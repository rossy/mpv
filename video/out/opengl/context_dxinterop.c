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
#include <versionhelpers.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwmapi.h>

#include "d3d11_helpers.h"
#include "common/common.h"
#include "options/m_config.h"
#include "video/out/w32_common.h"
#include "osdep/windows_utils.h"
#include "context.h"

// For WGL_ACCESS_WRITE_DISCARD_NV, etc.
#include <GL/wglext.h>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)

enum {
    PRESENTER_AUTO,
    PRESENTER_D3D9,
    PRESENTER_D3D11,
};

struct dxinterop_opts {
    int presenter;
    int swapchain_length; // Currently only works with DXGI 1.2+
    int max_frame_latency;
    int flip;
};

#define OPT_BASE_STRUCT struct dxinterop_opts
const struct m_sub_options dxinterop_conf = {
    .opts = (const struct m_option[]) {
        OPT_CHOICE("dxinterop-presenter", presenter, 0,
                   ({"auto", PRESENTER_AUTO},
                    {"d3d9", PRESENTER_D3D9},
                    {"d3d11", PRESENTER_D3D11})),
        OPT_INTRANGE("dxinterop-swapchain-length", swapchain_length, 0, 2, 16),
        OPT_INTRANGE("dxinterop-max-frame-latency", max_frame_latency, 0, 1, 16),
        OPT_FLAG("dxinterop-flip", flip, 0),
        {0}
    },
    .defaults = &(const struct dxinterop_opts) {
        .presenter = PRESENTER_AUTO,
        // The length of the backbuffer queue shouldn't affect latency because
        // swap_buffers() always uses the backbuffer at the head of the queue
        // and presents it immediately. MSDN says there is a performance
        // penalty for having a short backbuffer queue and this seems to be
        // true, at least on Nvidia, where less than four backbuffers causes
        // very high CPU usage. Use six to be safe.
        .swapchain_length = 6,
        .max_frame_latency = 3,
        .flip = 1,
    },
    .size = sizeof(struct dxinterop_opts),
};

struct priv {
    // Direct3D 11 device and resources
    ID3D11Device *d3d11_device;
    ID3D11DeviceContext *d3d11_context;
    ID3D11Texture2D *d3d11_backbuffer;
    ID3D11Texture2D *d3d11_rtarget;
    IDXGISwapChain *dxgi_swapchain;

    // Direct3D 9 device and resources
    IDirect3DDevice9Ex *d3d9_device;
    IDirect3DSurface9 *d3d9_backbuffer;
    IDirect3DSurface9 *d3d9_rtarget;
    IDirect3DSwapChain9Ex *d3d9_swapchain;

    // DX_interop share handles
    HANDLE device_h;
    HANDLE rtarget_h;

    // OpenGL offscreen context
    HWND os_wnd;
    HDC os_dc;
    HGLRC os_ctx;

    // OpenGL resources
    GLuint texture;

    // Did we lose the device? (D3D9Ex only)
    bool lost_device;

    // Requested and current parameters
    int requested_swapinterval, swapinterval;
    int sc_width, sc_height;

    struct dxinterop_opts *opts;
};

static __thread MPGLContext *current_ctx;

static void update_sizes(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    p->sc_width = ctx->vo->dwidth ? ctx->vo->dwidth : 1;
    p->sc_height = ctx->vo->dheight ? ctx->vo->dheight : 1;
}

static void pump_message_loop(void)
{
    // We have a hidden window on this thread (for the OpenGL context,) so pump
    // its message loop at regular intervals to be safe
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&message);
}

static void *w32gpa(const GLubyte *procName)
{
    HMODULE oglmod;
    void *res = wglGetProcAddress(procName);
    if (res)
        return res;
    oglmod = GetModuleHandleW(L"opengl32.dll");
    return GetProcAddress(oglmod, procName);
}

static void os_ctx_destroy(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;

    if (p->os_ctx) {
        wglMakeCurrent(p->os_dc, NULL);
        wglDeleteContext(p->os_ctx);
    }
    p->os_ctx = NULL;
    if (p->os_dc)
        ReleaseDC(p->os_wnd, p->os_dc);
    p->os_dc = NULL;
    if (p->os_wnd)
        DestroyWindow(p->os_wnd);
    p->os_wnd = NULL;
}

static bool os_ctx_create(MPGLContext *ctx)
{
    static const wchar_t os_wnd_class[] = L"mpv offscreen gl";
    struct priv *p = ctx->priv;
    struct vo *vo = ctx->vo;
    HGLRC legacy_context = NULL;
    bool success = false;

    RegisterClassExW(&(WNDCLASSEXW) {
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_OWNDC,
        .lpfnWndProc = DefWindowProc,
        .hInstance = HINST_THISCOMPONENT,
        .lpszClassName = os_wnd_class,
    });

    // Create a hidden window for an offscreen OpenGL context. It might also be
    // possible to use the VO window, but MSDN recommends against drawing to
    // the same window with flip mode present and other APIs, so play it safe.
    p->os_wnd = CreateWindowExW(0, os_wnd_class, os_wnd_class, 0, 0, 0, 200,
        200, NULL, NULL, HINST_THISCOMPONENT, NULL);
    p->os_dc = GetDC(p->os_wnd);
    if (!p->os_dc) {
        MP_FATAL(vo, "Couldn't create window for offscreen rendering\n");
        goto done;
    }

    // Choose a pixel format. It probably doesn't matter what this is because
    // the primary framebuffer will not be used.
    PIXELFORMATDESCRIPTOR pfd = {
        .nSize = sizeof pfd,
        .nVersion = 1,
        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        .iPixelType = PFD_TYPE_RGBA,
        .cColorBits = 24,
        .iLayerType = PFD_MAIN_PLANE,
    };
    int pf = ChoosePixelFormat(p->os_dc, &pfd);
    if (!pf) {
        MP_FATAL(vo, "Couldn't choose pixelformat for offscreen rendering: %s\n",
                 mp_LastError_to_str());
        goto done;
    }
    SetPixelFormat(p->os_dc, pf, &pfd);

    legacy_context = wglCreateContext(p->os_dc);
    if (!legacy_context || !wglMakeCurrent(p->os_dc, legacy_context)) {
        MP_FATAL(vo, "Couldn't create OpenGL context for offscreen rendering: %s\n",
                 mp_LastError_to_str());
        goto done;
    }

    const char *(GLAPIENTRY *wglGetExtensionsStringARB)(HDC hdc)
        = w32gpa((const GLubyte*)"wglGetExtensionsStringARB");
    if (!wglGetExtensionsStringARB) {
        MP_FATAL(vo, "The OpenGL driver does not support OpenGL 3.x\n");
        goto done;
    }

    const char *wgl_exts = wglGetExtensionsStringARB(p->os_dc);
    if (!strstr(wgl_exts, "WGL_ARB_create_context")) {
        MP_FATAL(vo, "The OpenGL driver does not support OpenGL 3.x\n");
        goto done;
    }

    HGLRC (GLAPIENTRY *wglCreateContextAttribsARB)(HDC hDC, HGLRC hShareContext,
                                                   const int *attribList)
        = w32gpa((const GLubyte*)"wglCreateContextAttribsARB");
    if (!wglCreateContextAttribsARB) {
        MP_FATAL(vo, "The OpenGL driver does not support OpenGL 3.x\n");
        goto done;
    }

    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 0,
        WGL_CONTEXT_FLAGS_ARB, 0,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    p->os_ctx = wglCreateContextAttribsARB(p->os_dc, 0, attribs);
    if (!p->os_ctx) {
        // NVidia, instead of ignoring WGL_CONTEXT_FLAGS_ARB, will error out if
        // it's present on pre-3.2 contexts.
        // Remove it from attribs and retry the context creation.
        attribs[6] = attribs[7] = 0;
        p->os_ctx = wglCreateContextAttribsARB(p->os_dc, 0, attribs);
    }
    if (!p->os_ctx) {
        MP_FATAL(vo,
                 "Couldn't create OpenGL 3.x context for offscreen rendering: %s\n",
                 mp_LastError_to_str());
        goto done;
    }

    wglMakeCurrent(p->os_dc, NULL);
    wglDeleteContext(legacy_context);
    legacy_context = NULL;

    if (!wglMakeCurrent(p->os_dc, p->os_ctx)) {
        MP_FATAL(vo,
                 "Couldn't activate OpenGL 3.x context for offscreen rendering: %s\n",
                 mp_LastError_to_str());
        goto done;
    }

    mpgl_load_functions(ctx->gl, w32gpa, wgl_exts, vo->log);
    if (!(ctx->gl->mpgl_caps & (MPGL_CAP_DXINTEROP | MPGL_CAP_DXINTEROP2))) {
        MP_FATAL(vo, "WGL_NV_DX_interop is not supported\n");
        goto done;
    }

    success = true;
done:
    if (legacy_context) {
        wglMakeCurrent(p->os_dc, NULL);
        wglDeleteContext(legacy_context);
    }
    if (!success)
        os_ctx_destroy(ctx);
    return success;
}

static void d3d11_swapchain_destroy(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    SAFE_RELEASE(p->dxgi_swapchain);
}

static bool d3d11_swapchain_create(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct vo *vo = ctx->vo;
    struct dxinterop_opts *o = p->opts;

    if (!p->d3d11_device)
        return false;

    update_sizes(ctx);
    struct d3d11_swapchain_opts swapchain_opts = {
        .window = vo_w32_hwnd(vo),
        .width = p->sc_width,
        .height = p->sc_height,
        .flip = o->flip,
        .length = o->swapchain_length,
        .usage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
    };
    return mp_d3d11_create_swapchain(p->d3d11_device, vo->log, &swapchain_opts,
                                     &p->dxgi_swapchain);
}

static void d3d11_size_dependent_destroy(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct GL *gl = ctx->gl;

    if (p->d3d11_rtarget && p->device_h && p->rtarget_h) {
        gl->DXUnlockObjectsNV(p->device_h, 1, &p->rtarget_h);
        gl->DXUnregisterObjectNV(p->device_h, p->rtarget_h);
        p->rtarget_h = 0;
    }
    if (p->d3d11_rtarget && p->texture) {
        gl->DeleteTextures(1, &p->texture);
        p->texture = 0;
    }

    SAFE_RELEASE(p->d3d11_rtarget);
    SAFE_RELEASE(p->d3d11_backbuffer);
}

static bool d3d11_size_dependent_create(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct vo *vo = ctx->vo;
    struct GL *gl = ctx->gl;
    bool success = false;
    HRESULT hr;

    hr = IDXGISwapChain_GetBuffer(p->dxgi_swapchain, 0, &IID_ID3D11Texture2D,
        (void**)&p->d3d11_backbuffer);
    if (FAILED(hr)) {
        MP_FATAL(vo, "Failed get backbuffer: %s\n", mp_HRESULT_to_str(hr));
        goto done;
    }

    // Get the format of the backbuffer
    D3D11_TEXTURE2D_DESC bb_desc = { 0 };
    ID3D11Texture2D_GetDesc(p->d3d11_backbuffer, &bb_desc);

    MP_VERBOSE(vo, "DX_interop backbuffer size: %ux%u\n",
        (unsigned)bb_desc.Width, (unsigned)bb_desc.Height);
    MP_VERBOSE(vo, "DX_interop backbuffer format: %u\n",
        (unsigned)bb_desc.Format);

    // Create a texture with the same format as the backbuffer for rendering
    // from OpenGL
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = bb_desc.Width,
        .Height = bb_desc.Height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = bb_desc.Format,
        .SampleDesc = { .Count = 1 },
        .BindFlags = D3D11_BIND_RENDER_TARGET,
        // Not needed on NVIDIA, but may be needed on AMD and Intel
        .MiscFlags = D3D11_RESOURCE_MISC_SHARED,
    };
    hr = ID3D11Device_CreateTexture2D(p->d3d11_device, &desc, NULL,
        &p->d3d11_rtarget);
    if (FAILED(hr)) {
        MP_FATAL(vo, "Couldn't create rendertarget\n");
        goto done;
    }

    // Create the OpenGL-side texture
    gl->GenTextures(1, &p->texture);

    // Now share the rendertarget with OpenGL as a texture
    p->rtarget_h = gl->DXRegisterObjectNV(p->device_h, p->d3d11_rtarget,
        p->texture, GL_TEXTURE_2D, WGL_ACCESS_WRITE_DISCARD_NV);
    if (!p->rtarget_h) {
        MP_ERR(vo, "Couldn't share rendertarget with OpenGL: %s\n",
               mp_LastError_to_str());
        goto done;
    }

    // Lock the rendertarget for use from OpenGL. This will only be unlocked in
    // swap_buffers() when it is blitted to the real Direct3D backbuffer.
    if (!gl->DXLockObjectsNV(p->device_h, 1, &p->rtarget_h)) {
        MP_ERR(vo, "Couldn't lock rendertarget: %s\n", mp_LastError_to_str());
        goto done;
    }

    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->main_fb);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, p->texture, 0);
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);

    success = true;
done:
    if (!success)
        d3d11_size_dependent_destroy(ctx);
    return success;
}

static void d3d9_size_dependent_destroy(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct GL *gl = ctx->gl;

    if (p->d3d9_rtarget && p->device_h && p->rtarget_h) {
        gl->DXUnlockObjectsNV(p->device_h, 1, &p->rtarget_h);
        gl->DXUnregisterObjectNV(p->device_h, p->rtarget_h);
        p->rtarget_h = 0;
    }
    if (p->d3d9_rtarget && p->texture) {
        gl->DeleteTextures(1, &p->texture);
        p->texture = 0;
    }

    SAFE_RELEASE(p->d3d9_rtarget);
    SAFE_RELEASE(p->d3d9_backbuffer);
    SAFE_RELEASE(p->d3d9_swapchain);
}

static bool d3d9_size_dependent_create(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct vo *vo = ctx->vo;
    struct GL *gl = ctx->gl;
    HRESULT hr;
    bool success = false;

    IDirect3DSwapChain9 *sw9;
    hr = IDirect3DDevice9Ex_GetSwapChain(p->d3d9_device, 0, &sw9);
    if (FAILED(hr)) {
        MP_ERR(vo, "Couldn't get swap chain: %s\n", mp_HRESULT_to_str(hr));
        goto done;
    }

    hr = IDirect3DSwapChain9_QueryInterface(sw9, &IID_IDirect3DSwapChain9Ex,
        (void**)&p->d3d9_swapchain);
    if (FAILED(hr)) {
        SAFE_RELEASE(sw9);
        MP_ERR(vo, "Obtained swap chain is not IDirect3DSwapChain9Ex: %s\n",
               mp_HRESULT_to_str(hr));
        goto done;
    }
    SAFE_RELEASE(sw9);

    hr = IDirect3DSwapChain9Ex_GetBackBuffer(p->d3d9_swapchain, 0,
        D3DBACKBUFFER_TYPE_MONO, &p->d3d9_backbuffer);
    if (FAILED(hr)) {
        MP_ERR(vo, "Couldn't get backbuffer: %s\n", mp_HRESULT_to_str(hr));
        goto done;
    }

    // Get the format of the backbuffer
    D3DSURFACE_DESC bb_desc = { 0 };
    IDirect3DSurface9_GetDesc(p->d3d9_backbuffer, &bb_desc);

    MP_VERBOSE(vo, "DX_interop backbuffer size: %ux%u\n",
        (unsigned)bb_desc.Width, (unsigned)bb_desc.Height);
    MP_VERBOSE(vo, "DX_interop backbuffer format: %u\n",
        (unsigned)bb_desc.Format);

    // Create a rendertarget with the same format as the backbuffer for
    // rendering from OpenGL
    HANDLE share_handle = NULL;
    hr = IDirect3DDevice9Ex_CreateRenderTarget(p->d3d9_device, bb_desc.Width,
        bb_desc.Height, bb_desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
        &p->d3d9_rtarget, &share_handle);
    if (FAILED(hr)) {
        MP_ERR(vo, "Couldn't create rendertarget: %s\n", mp_HRESULT_to_str(hr));
        goto done;
    }

    // Register the share handle with WGL_NV_DX_interop. Nvidia does not
    // require the use of share handles, but Intel does.
    if (share_handle)
        gl->DXSetResourceShareHandleNV(p->d3d9_rtarget, share_handle);

    // Create the OpenGL-side texture
    gl->GenTextures(1, &p->texture);

    // Now share the rendertarget with OpenGL as a texture
    p->rtarget_h = gl->DXRegisterObjectNV(p->device_h, p->d3d9_rtarget,
        p->texture, GL_TEXTURE_2D, WGL_ACCESS_WRITE_DISCARD_NV);
    if (!p->rtarget_h) {
        MP_ERR(vo, "Couldn't share rendertarget with OpenGL: %s\n",
               mp_LastError_to_str());
        goto done;
    }

    // Lock the rendertarget for use from OpenGL. This will only be unlocked in
    // swap_buffers() when it is blitted to the real Direct3D backbuffer.
    if (!gl->DXLockObjectsNV(p->device_h, 1, &p->rtarget_h)) {
        MP_ERR(vo, "Couldn't lock rendertarget: %s\n", mp_LastError_to_str());
        goto done;
    }

    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->main_fb);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, p->texture, 0);
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);

    success = true;
done:
    if (!success)
        d3d9_size_dependent_destroy(ctx);
    return success;
}

static void fill_presentparams(MPGLContext *ctx, D3DPRESENT_PARAMETERS *pparams)
{
    struct priv *p = ctx->priv;
    struct vo *vo = ctx->vo;
    struct dxinterop_opts *o = p->opts;

    // Present intervals other than IMMEDIATE and ONE don't seem to work. It's
    // possible that they're not compatible with FLIPEX.
    UINT presentation_interval;
    switch (p->requested_swapinterval) {
    case 0:  presentation_interval = D3DPRESENT_INTERVAL_IMMEDIATE; break;
    case 1:  presentation_interval = D3DPRESENT_INTERVAL_ONE;       break;
    default: presentation_interval = D3DPRESENT_INTERVAL_ONE;       break;
    }

    D3DSWAPEFFECT seffect = D3DSWAPEFFECT_FLIP;
    if (o->flip && IsWindows7OrGreater())
        seffect = D3DSWAPEFFECT_FLIPEX;

    *pparams = (D3DPRESENT_PARAMETERS) {
        .Windowed = TRUE,
        .BackBufferWidth = vo->dwidth ? vo->dwidth : 1,
        .BackBufferHeight = vo->dheight ? vo->dheight : 1,
        .BackBufferCount = o->swapchain_length,
        .SwapEffect = seffect,
        // Automatically get the backbuffer format from the display format
        .BackBufferFormat = D3DFMT_UNKNOWN,
        .PresentationInterval = presentation_interval,
        .hDeviceWindow = vo_w32_hwnd(vo),
    };
}

static void d3d11_device_destroy(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    SAFE_RELEASE(p->d3d11_context);
    SAFE_RELEASE(p->d3d11_device);
}

static bool d3d11_device_create(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct vo *vo = ctx->vo;
    struct GL *gl = ctx->gl;
    struct dxinterop_opts *o = p->opts;
    bool success = false;

    if (!(gl->mpgl_caps & MPGL_CAP_DXINTEROP2))
        goto done;

    struct d3d11_device_opts device_opts = {
        .allow_warp = false,
        .max_frame_latency = o->max_frame_latency,
    };
    if (!mp_d3d11_create_present_device(vo->log, &device_opts, &p->d3d11_device))
        goto done;
    ID3D11Device_GetImmediateContext(p->d3d11_device, &p->d3d11_context);

    // Register the Direct3D device with WGL_NV_dx_interop2
    p->device_h = gl->DXOpenDeviceNV(p->d3d11_device);
    if (!p->device_h) {
        MP_FATAL(vo, "Couldn't open Direct3D device from OpenGL: %s\n",
                 mp_LastError_to_str());
        goto done;
    }

    success = true;
done:
    if (!success)
        d3d11_device_destroy(ctx);
    return success;
}

static void d3d9_destroy(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct GL *gl = ctx->gl;

    if (p->d3d9_device && p->device_h) {
        gl->DXCloseDeviceNV(p->device_h);
        p->device_h = NULL;
    }
    SAFE_RELEASE(p->d3d9_device);
}

static bool d3d9_create(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct vo *vo = ctx->vo;
    struct GL *gl = ctx->gl;
    struct dxinterop_opts *o = p->opts;
    IDirect3D9Ex *d3d9ex = NULL;
    bool success = false;
    HRESULT hr;

    if (!(gl->mpgl_caps & MPGL_CAP_DXINTEROP))
        goto done;

    HMODULE d3d9 = LoadLibraryW(L"d3d9.dll");
    if (!d3d9) {
        MP_FATAL(vo, "Failed to load \"d3d9.dll\": %s\n", mp_LastError_to_str());
        goto done;
    }

    // WGL_NV_dx_interop requires Direct3D 9Ex on WDDM systems. Direct3D 9Ex
    // also enables flip mode present for efficient rendering with the DWM.
    HRESULT (WINAPI *pDirect3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex **ppD3D);
    pDirect3DCreate9Ex = (void*)GetProcAddress(d3d9, "Direct3DCreate9Ex");
    if (!pDirect3DCreate9Ex) {
        MP_FATAL(vo, "Direct3D 9Ex not supported\n");
        goto done;
    }

    hr = pDirect3DCreate9Ex(D3D_SDK_VERSION, &d3d9ex);
    if (FAILED(hr)) {
        MP_FATAL(vo, "Couldn't create Direct3D9Ex: %s\n", mp_HRESULT_to_str(hr));
        goto done;
    }

    D3DPRESENT_PARAMETERS pparams;
    fill_presentparams(ctx, &pparams);

    hr = IDirect3D9Ex_CreateDeviceEx(d3d9ex, D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL, vo_w32_hwnd(vo),
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE |
        D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED |
        D3DCREATE_NOWINDOWCHANGES,
        &pparams, NULL, &p->d3d9_device);
    if (FAILED(hr)) {
        MP_FATAL(vo, "Couldn't create device: %s\n", mp_HRESULT_to_str(hr));
        goto done;
    }
    MP_VERBOSE(vo, "Using Direct3D9\n");

    IDirect3DDevice9Ex_SetMaximumFrameLatency(p->d3d9_device,
                                              o->max_frame_latency);

    // Register the Direct3D device with WGL_NV_dx_interop
    p->device_h = gl->DXOpenDeviceNV(p->d3d9_device);
    if (!p->device_h) {
        MP_FATAL(vo, "Couldn't open Direct3D device from OpenGL: %s\n",
                 mp_LastError_to_str());
        goto done;
    }

    success = true;
done:
    SAFE_RELEASE(d3d9ex);
    if (!success)
        d3d9_destroy(ctx);
    return success;
}

static void dxinterop_uninit(MPGLContext *ctx)
{
    d3d11_size_dependent_destroy(ctx);
    d3d11_swapchain_destroy(ctx);
    d3d11_device_destroy(ctx);
    d3d9_size_dependent_destroy(ctx);
    d3d9_destroy(ctx);
    os_ctx_destroy(ctx);
    vo_w32_uninit(ctx->vo);
    DwmEnableMMCSS(FALSE);
    pump_message_loop();
}

static void d3d11_backbuffer_resize(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct vo *vo = ctx->vo;
    HRESULT hr;

    int old_sc_width = p->sc_width;
    int old_sc_height = p->sc_height;
    update_sizes(ctx);

    // Avoid unnecessary resizing
    if (old_sc_width == p->sc_width && old_sc_height == p->sc_height)
        return;

    // All references to backbuffers must be released before ResizeBuffers
    d3d11_size_dependent_destroy(ctx);

    // The DirectX runtime may report errors related to the device like
    // DXGI_ERROR_DEVICE_REMOVED at this point
    hr = IDXGISwapChain_ResizeBuffers(p->dxgi_swapchain, 0, p->sc_width,
        p->sc_height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
        MP_FATAL(vo, "Couldn't resize swapchain: %s\n", mp_HRESULT_to_str(hr));

    if (!d3d11_size_dependent_create(ctx))
        MP_FATAL(vo, "Couldn't get back buffer after resize\n");
}

static void d3d9_reset(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct vo *vo = ctx->vo;
    HRESULT hr;

    int old_sc_width = p->sc_width;
    int old_sc_height = p->sc_height;
    update_sizes(ctx);

    // Check if the device actually needs to be reset
    if (old_sc_width == p->sc_width && old_sc_height == p->sc_height &&
        p->requested_swapinterval == p->swapinterval && !p->lost_device)
        return;

    d3d9_size_dependent_destroy(ctx);

    D3DPRESENT_PARAMETERS pparams;
    fill_presentparams(ctx, &pparams);

    hr = IDirect3DDevice9Ex_ResetEx(p->d3d9_device, &pparams, NULL);
    if (FAILED(hr)) {
        p->lost_device = true;
        MP_ERR(vo, "Couldn't reset device: %s\n", mp_HRESULT_to_str(hr));
        return;
    }

    if (!d3d9_size_dependent_create(ctx)) {
        p->lost_device = true;
        MP_ERR(vo, "Couldn't recreate Direct3D objects after reset\n");
        return;
    }

    MP_VERBOSE(vo, "Direct3D device reset\n");
    p->swapinterval = p->requested_swapinterval;
    p->lost_device = false;
}

static int GLAPIENTRY dxinterop_swap_interval(int interval)
{
    if (!current_ctx)
        return 0;
    struct priv *p = current_ctx->priv;

    if (p->d3d9_device) {
        // Direct3D9 devices must be reset to change swapchain parameters
        p->requested_swapinterval = interval;
        d3d9_reset(current_ctx);
    } else {
        p->swapinterval = interval;
    }
    return 1;
}

static void * GLAPIENTRY dxinterop_get_native_display(const char *name)
{
    if (!current_ctx || !name)
        return NULL;
    struct priv *p = current_ctx->priv;

    if (p->d3d9_device && strcmp("IDirect3DDevice9Ex", name) == 0) {
        return p->d3d9_device;
    } else if (p->device_h && strcmp("dxinterop_device_HANDLE", name) == 0) {
        return p->device_h;
    }
    return NULL;
}

static int dxinterop_init(MPGLContext *ctx, int flags)
{
    struct priv *p = ctx->priv;
    struct GL *gl = ctx->gl;

    p->opts = mp_get_config_group(ctx, ctx->global, &dxinterop_conf);
    struct dxinterop_opts *o = p->opts;

    // Windows 7's compositor uses D3D9, it only supports flip-model present
    // with the platform update and it doesn't have D3D11VA, so just pick D3D9
    // when auto-selecting the presenter.
    int presenter = o->presenter;
    if (presenter == PRESENTER_AUTO && !IsWindows8OrGreater())
        presenter = PRESENTER_D3D9;

    p->requested_swapinterval = 1;

    if (!vo_w32_init(ctx->vo))
        goto fail;
    if (!os_ctx_create(ctx))
        goto fail;

    // Create the shared framebuffer
    gl->GenFramebuffers(1, &ctx->main_fb);

    current_ctx = ctx;
    gl->SwapInterval = dxinterop_swap_interval;
    gl->MPGetNativeDisplay = dxinterop_get_native_display;

    bool device_ok = false;
    if ((!device_ok && !presenter) || presenter == PRESENTER_D3D11) {
        device_ok = d3d11_device_create(ctx);
        if (device_ok)
            device_ok = d3d11_swapchain_create(ctx);
        if (device_ok)
            device_ok = d3d11_size_dependent_create(ctx);
    }
    if ((!device_ok && !presenter) || presenter == PRESENTER_D3D9) {
        device_ok = d3d9_create(ctx);
        if (device_ok)
            device_ok = d3d9_size_dependent_create(ctx);
    }
    if (!device_ok)
        goto fail;

    // The OpenGL and Direct3D coordinate systems are flipped vertically
    // relative to each other. Flip the video during rendering so it can be
    // copied to the Direct3D backbuffer with a simple (and fast) StretchRect.
    ctx->flip_v = true;

    DwmEnableMMCSS(TRUE);

    return 0;
fail:
    dxinterop_uninit(ctx);
    return -1;
}

static int dxinterop_reconfig(MPGLContext *ctx)
{
    vo_w32_config(ctx->vo);
    return 0;
}

static void d3d11_swap_buffers(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct vo *vo = ctx->vo;
    struct GL *gl = ctx->gl;
    ID3D11Texture2D *bbuffer = NULL;
    HRESULT hr;

    pump_message_loop();

    if (!gl->DXUnlockObjectsNV(p->device_h, 1, &p->rtarget_h)) {
        MP_ERR(vo, "Couldn't unlock rendertarget for present: %s\n",
               mp_LastError_to_str());
        goto done;
    }

    ID3D11DeviceContext_CopyResource(p->d3d11_context,
        (ID3D11Resource*)p->d3d11_backbuffer,
        (ID3D11Resource*)p->d3d11_rtarget);

    hr = IDXGISwapChain_Present(p->dxgi_swapchain, p->swapinterval, 0);
    if (FAILED(hr))
        MP_FATAL(vo, "Failed to present: %s\n", mp_HRESULT_to_str(hr));

    if (!gl->DXLockObjectsNV(p->device_h, 1, &p->rtarget_h)) {
        MP_ERR(vo, "Couldn't lock rendertarget after present: %s\n",
               mp_LastError_to_str());
    }

done:
    SAFE_RELEASE(bbuffer);
}

static void d3d9_swap_buffers(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    struct vo *vo = ctx->vo;
    struct GL *gl = ctx->gl;
    HRESULT hr;

    pump_message_loop();

    // If the device is still lost, try to reset it again
    if (p->lost_device)
        d3d9_reset(ctx);
    if (p->lost_device)
        return;

    if (!gl->DXUnlockObjectsNV(p->device_h, 1, &p->rtarget_h)) {
        MP_ERR(vo, "Couldn't unlock rendertarget for present: %s\n",
               mp_LastError_to_str());
        return;
    }

    // Blit the OpenGL rendertarget to the backbuffer
    hr = IDirect3DDevice9Ex_StretchRect(p->d3d9_device, p->d3d9_rtarget, NULL,
                                        p->d3d9_backbuffer, NULL, D3DTEXF_NONE);
    if (FAILED(hr)) {
        MP_ERR(vo, "Couldn't stretchrect for present: %s\n",
               mp_HRESULT_to_str(hr));
        return;
    }

    hr = IDirect3DDevice9Ex_PresentEx(p->d3d9_device, NULL, NULL, NULL, NULL, 0);

    if (!gl->DXLockObjectsNV(p->device_h, 1, &p->rtarget_h)) {
        MP_ERR(vo, "Couldn't lock rendertarget after present: %s\n",
               mp_LastError_to_str());
    }

    switch (hr) {
    case D3DERR_DEVICELOST:
    case D3DERR_DEVICEHUNG:
        MP_VERBOSE(vo, "Direct3D device lost! Resetting.\n");
        p->lost_device = true;
        d3d9_reset(ctx);
        return;
    default:
        if (FAILED(hr))
            MP_ERR(vo, "Failed to present: %s\n", mp_HRESULT_to_str(hr));
    }
}

static void dxinterop_swap_buffers(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    if (p->dxgi_swapchain) {
        d3d11_swap_buffers(ctx);
    } else {
        d3d9_swap_buffers(ctx);
    }
}

static int dxinterop_control(MPGLContext *ctx, int *events, int request,
                             void *arg)
{
    struct priv *p = ctx->priv;

    int r = vo_w32_control(ctx->vo, events, request, arg);
    if (*events & VO_EVENT_RESIZE) {
        if (p->dxgi_swapchain) {
            d3d11_backbuffer_resize(ctx);
        } else {
            d3d9_reset(ctx);
        }
    }
    return r;
}

const struct mpgl_driver mpgl_driver_dxinterop = {
    .name         = "dxinterop",
    .priv_size    = sizeof(struct priv),
    .init         = dxinterop_init,
    .reconfig     = dxinterop_reconfig,
    .swap_buffers = dxinterop_swap_buffers,
    .control      = dxinterop_control,
    .uninit       = dxinterop_uninit,
};
