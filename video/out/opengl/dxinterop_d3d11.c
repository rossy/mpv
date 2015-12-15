/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp_c.h>
#include <dwmapi.h>

#include "dxinterop_common.h"

#define DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT (64)

struct priv {
    struct offscreen_gl osgl;

    HMODULE d3d11_dll;
    PFN_D3D11_CREATE_DEVICE D3D11CreateDevice;

    HMODULE dcomp_dll;
    HRESULT (WINAPI *DCompositionCreateDevice)(IDXGIDevice*, REFIID, void**);

    ID3D11Device *device;
    HANDLE device_h;
    ID3D11DeviceContext *ctx;
    IDXGIDevice1 *device_dxgi;
    IDXGIAdapter1 *adapter;
    IDXGIFactory1 *factory;
    IDXGIFactory2 *factory2;
    IDXGISwapChain *swapchain;
    IDXGISwapChain1 *swapchain1;
    IDXGISwapChain2 *swapchain2;
    ID3D11Texture2D *backbuffer;
    HANDLE backbuffer_h;
    ID3D11Texture2D *rendertarget;
    HANDLE rendertarget_h;

    IDCompositionDevice *dcomp;
    IDCompositionTarget *dcomp_target;
    IDCompositionVisual *dcomp_visual;

    UINT swapchain_flags;
    HANDLE frame_event;

    bool fb_bound;
    bool rb_attached;

    GLuint framebuffer;
    GLuint renderbuffer;

    int width, height, sc_width, sc_height;

    void (GLAPIENTRY *real_gl_bind_framebuffer)(GLenum, GLuint);
};

static __thread struct MPGLContext *current_ctx;

static void pump_message_loop(void)
{
    // We have a hidden window on this thread (for the OpenGL context,) so pump
    // its message loop at regular intervals to be safe
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&message);
}

static void update_sizes(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;

    p->width = ctx->vo->dwidth;
    p->height = ctx->vo->dheight;

    if (p->swapchain2) {
        // Use fuzzy texture sizes to avoid reallocations
        if (p->width > p->sc_width)
            p->sc_width = MP_ALIGN_UP(p->width, 256);
        if (p->height > p->sc_height)
            p->sc_height = MP_ALIGN_UP(p->height, 256);
    } else {
        p->sc_width = p->width;
        p->sc_height = p->height;
    }

    if (p->sc_width == 0)
        p->sc_width = 1;
    if (p->sc_height == 0)
        p->sc_height = 1;
}

static int swapchain_create(MPGLContext *ctx, int flags)
{
    struct priv *p = ctx->priv;
    struct GL *gl = ctx->gl;
    HRESULT hr;

    if (p->factory2) {
        update_sizes(ctx);

        DXGI_SWAP_CHAIN_DESC1 desc1 = {
            .Width = p->sc_width,
            .Height = p->sc_height,
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .SampleDesc = { .Count = 1 },
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = 6,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
            .Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT,
        };

        if (p->dcomp) {
            if (flags & VOFLAG_ALPHA) {
                desc1.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
                gl->mpgl_caps |= MPGL_CAP_PREMUL_ALPHA;
            }

            hr = IDXGIFactory2_CreateSwapChainForComposition(p->factory2,
                (IUnknown*)p->device, &desc1, NULL, &p->swapchain1);
            if (FAILED(hr)) {
                desc1.Flags = 0;
                hr = IDXGIFactory2_CreateSwapChainForComposition(p->factory2,
                    (IUnknown*)p->device, &desc1, NULL, &p->swapchain1);
            }
        } else {
            hr = IDXGIFactory2_CreateSwapChainForHwnd(p->factory2,
                (IUnknown*)p->device, vo_w32_hwnd(ctx->vo), &desc1, NULL, NULL,
                &p->swapchain1);
            if (FAILED(hr)) {
                desc1.Flags = 0;
                hr = IDXGIFactory2_CreateSwapChainForHwnd(p->factory2,
                    (IUnknown*)p->device, vo_w32_hwnd(ctx->vo), &desc1, NULL,
                    NULL, &p->swapchain1);
            }
        }
        if (FAILED(hr)) {
            MP_FATAL(ctx->vo, "Couldn't create DXGI 1.2+ swap chain\n");
            return -1;
        }
        p->swapchain_flags = desc1.Flags;

        if (p->dcomp) {
            hr = IDCompositionVisual_SetContent(p->dcomp_visual,
                (IUnknown*)p->swapchain1);
            if (FAILED(hr) || FAILED(IDCompositionDevice_Commit(p->dcomp))) {
                MP_FATAL(ctx->vo, "Couldn't set DirectComposition visual\n");
                return -1;
            }
        }

        hr = IDXGISwapChain1_QueryInterface(p->swapchain1, &IID_IDXGISwapChain,
            (void**)&p->swapchain);
        if (FAILED(hr)) {
            MP_FATAL(ctx->vo, "Couldn't create DXGI 1.2+ swap chain\n");
            return -1;
        }

        hr = IDXGISwapChain1_QueryInterface(p->swapchain1, &IID_IDXGISwapChain2,
            (void**)&p->swapchain2);
        if (p->swapchain2) {
            p->frame_event =
                IDXGISwapChain2_GetFrameLatencyWaitableObject(p->swapchain2);
            if (p->frame_event) {
                IDXGISwapChain2_SetMaximumFrameLatency(p->swapchain2, 1);
                WaitForSingleObject(p->frame_event, 1000);
            } else {
                MP_ERR(ctx->vo, "Couldn't get frame latency waitable object\n");
            }
        }

        return 0;
    }

    DXGI_SWAP_CHAIN_DESC desc = {
        .BufferDesc = { .Format = DXGI_FORMAT_R8G8B8A8_UNORM },
        .SampleDesc = { .Count = 1 },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 1,
        .OutputWindow = vo_w32_hwnd(ctx->vo),
        .Windowed = TRUE,
        .SwapEffect = DXGI_SWAP_EFFECT_DISCARD,
    };
    hr = IDXGIFactory1_CreateSwapChain(p->factory, (IUnknown*)p->device, &desc,
        &p->swapchain);
    if (FAILED(hr)) {
        MP_FATAL(ctx->vo, "Couldn't create DXGI 1.1 swap chain\n");
        return -1;
    }

    return 0;
}

static int dcomp_create(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    HRESULT hr;

    p->dcomp_dll = LoadLibraryW(L"dcomp.dll");
    if (!p->dcomp_dll) {
        MP_VERBOSE(ctx->vo, "\"dcomp.dll\" not found\n");
        return -1;
    }

    p->DCompositionCreateDevice = (void*)
        GetProcAddress(p->dcomp_dll, "DCompositionCreateDevice");
    if (!p->DCompositionCreateDevice) {
        MP_VERBOSE(ctx->vo, "DirectComposition not supported\n");
        return -1;
    }

    hr = p->DCompositionCreateDevice((IDXGIDevice*)p->device_dxgi,
        &IID_IDCompositionDevice, (void**)&p->dcomp);
    if (FAILED(hr)) {
        MP_VERBOSE(ctx->vo, "Couldn't create DirectComposition device\n");
        return -1;
    }

    hr = IDCompositionDevice_CreateTargetForHwnd(p->dcomp,
        vo_w32_hwnd(ctx->vo), FALSE, &p->dcomp_target);
    if (FAILED(hr)) {
        MP_VERBOSE(ctx->vo, "Couldn't create DirectComposition target\n");
        return -1;
    }

    hr = IDCompositionDevice_CreateVisual(p->dcomp, &p->dcomp_visual);
    if (FAILED(hr)) {
        MP_VERBOSE(ctx->vo, "Couldn't create DirectComposition visual\n");
        return -1;
    }

    hr = IDCompositionTarget_SetRoot(p->dcomp_target, p->dcomp_visual);
    if (FAILED(hr)) {
        MP_VERBOSE(ctx->vo, "Couldn't attach DirectComposition visual tree\n");
        return -1;
    }

    return 0;
}

static void dcomp_destroy(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;

    if (p->dcomp_target) {
        IDCompositionTarget_SetRoot(p->dcomp_target, NULL);
        IDCompositionTarget_Release(p->dcomp_target);
    }
    p->dcomp_target = NULL;
    if (p->dcomp_visual) {
        IDCompositionVisual_SetContent(p->dcomp_visual, NULL);
        IDCompositionVisual_Release(p->dcomp_visual);
    }
    p->dcomp_visual = NULL;
    if (p->dcomp)
        IDCompositionDevice_Release(p->dcomp);
    p->dcomp = NULL;
    if (p->dcomp_dll)
        FreeLibrary(p->dcomp_dll);
}

static int d3d_create(MPGLContext *ctx, int flags)
{
    struct priv *p = ctx->priv;
    struct GL *gl = ctx->gl;
    HRESULT hr;

    p->d3d11_dll = LoadLibraryW(L"d3d11.dll");
    if (!p->d3d11_dll) {
        MP_FATAL(ctx->vo, "\"d3d11.dll\" not found\n");
        return -1;
    }

    p->D3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)
        GetProcAddress(p->d3d11_dll, "D3D11CreateDevice");
    if (!p->D3D11CreateDevice) {
        MP_FATAL(ctx->vo, "Direct3D 11 not supported\n");
        return -1;
    }

    hr = p->D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL,
        0, D3D11_SDK_VERSION, &p->device, NULL, &p->ctx);
    if (FAILED(hr)) {
        MP_FATAL(ctx->vo, "Couldn't create Direct3D 11 device\n");
        return -1;
    }

    // Register the Direct3D device with WGL_NV_dx_interop2
    p->device_h = gl->DXOpenDeviceNV(p->device);
    if (!p->device_h) {
        MP_FATAL(ctx->vo, "Couldn't open Direct3D from GL\n");
        return -1;
    }

    hr = ID3D11Device_QueryInterface(p->device, &IID_IDXGIDevice1,
        (void**)&p->device_dxgi);
    if (FAILED(hr)) {
        MP_FATAL(ctx->vo, "Couldn't get DXGI device\n");
        return -1;
    }

    // mpv expects frames to be presented right after swap_buffers() returns
    IDXGIDevice1_SetMaximumFrameLatency(p->device_dxgi, 1);

    if (dcomp_create(ctx) >= 0) {
        MP_VERBOSE(ctx->vo, "Using DirectComposition\n");
    } else {
        dcomp_destroy(ctx);
        MP_VERBOSE(ctx->vo, "Not using DirectComposition\n");
    }

    hr = IDXGIDevice1_GetParent(p->device_dxgi, &IID_IDXGIAdapter1,
        (void**)&p->adapter);
    if (FAILED(hr)) {
        MP_FATAL(ctx->vo, "Couldn't get DXGI adapter\n");
        return -1;
    }

    hr = IDXGIAdapter1_GetParent(p->adapter, &IID_IDXGIFactory1,
        (void**)&p->factory);
    if (FAILED(hr)) {
        MP_FATAL(ctx->vo, "Couldn't get DXGI factory\n");
        return -1;
    }

    IDXGIFactory1_QueryInterface(p->factory, &IID_IDXGIFactory2,
        (void**)&p->factory2);

    if (swapchain_create(ctx, flags) < 0)
        return -1;

    IDXGIFactory1_MakeWindowAssociation(p->factory, vo_w32_hwnd(ctx->vo),
        DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);

    if (p->swapchain2)
        MP_VERBOSE(ctx->vo, "Using DXGI 1.3\n");
    else if (p->swapchain1)
        MP_VERBOSE(ctx->vo, "Using DXGI 1.2\n");
    else
        MP_VERBOSE(ctx->vo, "Using DXGI 1.1\n");

    return 0;
}

static void d3d_destroy(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct GL *gl = ctx->gl;

    dcomp_destroy(ctx);

    if (p->device_h)
        gl->DXCloseDeviceNV(p->device_h);
    if (p->swapchain)
        IDXGISwapChain_Release(p->swapchain);
    if (p->swapchain1)
        IDXGISwapChain1_Release(p->swapchain1);
    if (p->swapchain2)
        IDXGISwapChain2_Release(p->swapchain2);
    if (p->adapter)
        IDXGIAdapter1_Release(p->adapter);
    if (p->factory)
        IDXGIFactory1_Release(p->factory);
    if (p->factory2)
        IDXGIFactory2_Release(p->factory2);
    if (p->device_dxgi)
        IDXGIDevice1_Release(p->device_dxgi);
    if (p->ctx)
        ID3D11DeviceContext_Release(p->ctx);
    if (p->device)
        ID3D11Device_Release(p->device);
    if (p->d3d11_dll)
        FreeLibrary(p->d3d11_dll);
}

static void try_attach_renderbuffer(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct GL *gl = ctx->gl;

    if (p->fb_bound && !p->rb_attached) {
        gl->FramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_RENDERBUFFER, p->renderbuffer);
        p->rb_attached = true;
    }
}

static int d3d_size_dependent_create(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct GL *gl = ctx->gl;
    HRESULT hr;

    hr = IDXGISwapChain_GetBuffer(p->swapchain, 0, &IID_ID3D11Texture2D,
        (void*)&p->backbuffer);
    if (FAILED(hr)) {
        MP_FATAL(ctx->vo, "Couldn't get backbuffer\n");
        return -1;
    }

    gl->GenRenderbuffers(1, &p->renderbuffer);
    p->rb_attached = false;

    // The backbuffer can only be shared directly with OpenGL when not using
    // flip present mode
    if (!p->swapchain1) {
        p->backbuffer_h = gl->DXRegisterObjectNV(p->device_h, p->backbuffer,
            p->renderbuffer, GL_RENDERBUFFER, WGL_ACCESS_WRITE_DISCARD_NV);
        if (!p->backbuffer_h) {
            MP_FATAL(ctx->vo, "Couldn't share backbuffer with GL: 0x%08x\n",
                (unsigned)GetLastError());
            return -1;
        }

        if (!gl->DXLockObjectsNV(p->device_h, 1, &p->backbuffer_h)) {
            MP_FATAL(ctx->vo, "Couldn't lock backbuffer\n");
            return -1;
        }
    }

    // When using flip present mode, the backbuffer can't be shared directly
    // with OpenGL, so create a rendertarget texture to render to
    if (p->swapchain1) {
        D3D11_TEXTURE2D_DESC bb_desc = { 0 };
        ID3D11Texture2D_GetDesc(p->backbuffer, &bb_desc);

        const D3D11_TEXTURE2D_DESC desc = {
            .Width = bb_desc.Width,
            .Height = bb_desc.Height,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = bb_desc.Format,
            .SampleDesc = { .Count = 1 },
            .BindFlags = D3D11_BIND_RENDER_TARGET,
        };

        hr = ID3D11Device_CreateTexture2D(p->device, &desc, NULL,
            &p->rendertarget);
        if (FAILED(hr)) {
            MP_FATAL(ctx->vo, "Couldn't create rendertarget\n");
            return -1;
        }

        p->rendertarget_h = gl->DXRegisterObjectNV(p->device_h, p->rendertarget,
            p->renderbuffer, GL_RENDERBUFFER, WGL_ACCESS_WRITE_DISCARD_NV);
        if (!p->rendertarget_h) {
            MP_FATAL(ctx->vo, "Couldn't share rendertarget with GL: 0x%08x\n",
                (unsigned)GetLastError());
            return -1;
        }

        if (!gl->DXLockObjectsNV(p->device_h, 1, &p->rendertarget_h)) {
            MP_FATAL(ctx->vo, "Couldn't lock rendertarget\n");
            return -1;
        }
    }

    try_attach_renderbuffer(ctx);
    return 0;
}

static void d3d_size_dependent_destroy(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct GL *gl = ctx->gl;

    if (p->rendertarget_h) {
        gl->DXUnlockObjectsNV(p->device_h, 1, &p->rendertarget_h);
        gl->DXUnregisterObjectNV(p->device_h, p->rendertarget_h);
    }
    p->rendertarget_h = NULL;
    if (p->rendertarget)
        ID3D11Texture2D_Release(p->rendertarget);
    p->rendertarget = NULL;
    if (p->backbuffer_h) {
        gl->DXUnlockObjectsNV(p->device_h, 1, &p->backbuffer_h);
        gl->DXUnregisterObjectNV(p->device_h, p->backbuffer_h);
    }
    p->backbuffer_h = NULL;
    if (p->backbuffer)
        ID3D11Texture2D_Release(p->backbuffer);
    p->backbuffer = NULL;
    if (p->renderbuffer)
        gl->DeleteRenderbuffers(1, &p->renderbuffer);
    p->renderbuffer = 0;
}

static void d3d11_uninit(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;

    d3d_size_dependent_destroy(ctx);
    d3d_destroy(ctx);
    mp_dxinterop_os_gl_destroy(ctx, &p->osgl);
    vo_w32_uninit(ctx->vo);
    DwmEnableMMCSS(FALSE);
    pump_message_loop();
}

static GLAPIENTRY void dxinterop_bind_framebuffer(GLenum target,
    GLuint framebuffer)
{
    if (!current_ctx)
        return;
    struct priv *p = current_ctx->priv;

    // Keep track of whether the shared framebuffer is bound
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
        p->fb_bound = (framebuffer == 0);

    // Pretend the shared framebuffer is the primary framebuffer
    if (framebuffer == 0)
        framebuffer = p->framebuffer;

    p->real_gl_bind_framebuffer(target, framebuffer);

    // Attach the shared texture if it is not attached already
    try_attach_renderbuffer(current_ctx);
}

static int d3d11_init(struct MPGLContext *ctx, int flags)
{
    struct priv *p = ctx->priv;
    struct GL *gl = ctx->gl;

    if (!vo_w32_init(ctx->vo))
        goto fail;
    if (mp_dxinterop_os_gl_init(ctx, &p->osgl) < 0)
        goto fail;

    // Create the shared framebuffer
    gl->GenFramebuffers(1, &p->framebuffer);

    // Hook glBindFramebuffer to return the shared framebuffer instead of the
    // primary one
    current_ctx = ctx;
    p->real_gl_bind_framebuffer = gl->BindFramebuffer;
    gl->BindFramebuffer = dxinterop_bind_framebuffer;

    if (d3d_create(ctx, flags) < 0)
        goto fail;
    if (d3d_size_dependent_create(ctx) < 0)
        goto fail;

    // This backend only supports 8-bit colour (for now?)
    ctx->depth_r = ctx->depth_g = ctx->depth_b = 8;

    // Bind the shared framebuffer. This will also attach the shared texture.
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);

    // The OpenGL and Direct3D coordinate systems are flipped vertically
    // relative to each other. Flip the video during rendering so it can be
    // copied to the Direct3D backbuffer with a simple (and fast) StretchRect.
    ctx->flip_v = true;

    DwmEnableMMCSS(TRUE);

    return 0;
fail:
    d3d11_uninit(ctx);
    return -1;
}

static int d3d11_reconfig(struct MPGLContext *ctx)
{
    vo_w32_config(ctx->vo);
    return 0;
}

static void d3d11_swap_buffers(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct GL *gl = ctx->gl;
    HRESULT hr;

    pump_message_loop();

    if (p->swapchain1) {
        if (!gl->DXUnlockObjectsNV(p->device_h, 1, &p->rendertarget_h)) {
            MP_FATAL(ctx->vo, "Couldn't unlock rendertarget for present\n");
            return;
        }

        if (p->swapchain2) {
            ID3D11DeviceContext_CopySubresourceRegion(p->ctx,
                (ID3D11Resource*)p->backbuffer, 0, 0, 0, 0,
                (ID3D11Resource*)p->rendertarget, 0,
                (&(D3D11_BOX) { 0, 0, 0, p->width, p->height, 1 }));
        } else {
            ID3D11DeviceContext_CopyResource(p->ctx,
                (ID3D11Resource*)p->backbuffer,
                (ID3D11Resource*)p->rendertarget);
        }

        if (!gl->DXLockObjectsNV(p->device_h, 1, &p->rendertarget_h)) {
            MP_FATAL(ctx->vo, "Couldn't lock rendertarget after blit\n");
            return;
        }
    } else {
        if (!gl->DXUnlockObjectsNV(p->device_h, 1, &p->backbuffer_h)) {
            MP_FATAL(ctx->vo, "Couldn't unlock backbuffer for present\n");
            return;
        }
    }

    hr = IDXGISwapChain_Present(p->swapchain, 1, 0);
    if (FAILED(hr))
        MP_FATAL(ctx->vo, "Couldn't present! 0x%08x\n", (unsigned)hr);

    if (!p->swapchain1) {
        if (!gl->DXLockObjectsNV(p->device_h, 1, &p->backbuffer_h)) {
            MP_FATAL(ctx->vo, "Couldn't lock backbuffer after present\n");
            return;
        }
    }

    if (p->frame_event)
        WaitForSingleObject(p->frame_event, 1000);
}

static void d3d_resize(MPGLContext *ctx)
{
    HRESULT hr;
    struct priv *p = ctx->priv;

    int old_sc_width = p->sc_width, old_sc_height = p->sc_height;
    update_sizes(ctx);

    if (p->sc_width != old_sc_width || p->sc_height != old_sc_height) {
        d3d_size_dependent_destroy(ctx);

        hr = IDXGISwapChain_ResizeBuffers(p->swapchain, 0, p->sc_width,
            p->sc_height, DXGI_FORMAT_UNKNOWN, p->swapchain_flags);
        if (FAILED(hr))
            MP_FATAL(ctx->vo, "Couldn't resize swapchain\n");

        if (d3d_size_dependent_create(ctx) < 0)
            MP_FATAL(ctx->vo, "Couldn't recreate resources after resize\n");
    }
    if (p->swapchain2) {
        IDXGISwapChain2_SetSourceSize(p->swapchain2, p->width, p->height);
        if (p->dcomp) {
            IDCompositionVisual_SetTransform(p->dcomp_visual,
                (&(D2D_MATRIX_3X2_F) {
                    ((float)p->width) / ((float)p->sc_width), 0,
                    0, ((float)p->height) / ((float)p->sc_height),
                    0, 0,
                }));
            IDCompositionDevice_Commit(p->dcomp);
        }
    }
}

static int d3d11_control(MPGLContext *ctx, int *events, int request, void *arg)
{
    int r = vo_w32_control(ctx->vo, events, request, arg);
    if (*events & VO_EVENT_RESIZE)
        d3d_resize(ctx);
    return r;
}

const struct mpgl_driver mpgl_driver_d3d11 = {
    .name         = "d3d11",
    .priv_size    = sizeof(struct priv),
    .init         = d3d11_init,
    .reconfig     = d3d11_reconfig,
    .swap_buffers = d3d11_swap_buffers,
    .control      = d3d11_control,
    .uninit       = d3d11_uninit,
};
