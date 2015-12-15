/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 *
 * You can alternatively redistribute this file and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <windows.h>
#include <initguid.h>

#include "dxinterop_common.h"

static void *w32gpa(const GLubyte *procName)
{
    HMODULE oglmod;
    void *res = wglGetProcAddress(procName);
    if (res)
        return res;
    oglmod = GetModuleHandleW(L"opengl32.dll");
    return GetProcAddress(oglmod, procName);
}

int mp_dxinterop_os_gl_init(struct MPGLContext *ctx, struct offscreen_gl *osgl)
{
    static const wchar_t os_wnd_class[] = L"mpv offscreen gl";
    HGLRC legacy_context = NULL;

    RegisterClassExW(&(WNDCLASSEXW) {
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_OWNDC,
        .lpfnWndProc = DefWindowProc,
        .hInstance = GetModuleHandleW(NULL),
        .lpszClassName = os_wnd_class,
    });

    // Create a hidden window for an offscreen OpenGL context. It might also be
    // possible to use the VO window, but MSDN recommends against drawing to
    // the same window with flip mode present and other APIs, so play it safe.
    osgl->wnd = CreateWindowExW(0, os_wnd_class, os_wnd_class, 0, 0, 0, 200,
        200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    osgl->dc = GetDC(osgl->wnd);
    if (!osgl->dc) {
        MP_FATAL(ctx->vo, "Couldn't create window for offscreen rendering\n");
        goto fail;
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
    int pf = ChoosePixelFormat(osgl->dc, &pfd);
    if (!pf) {
        MP_FATAL(ctx->vo, "Couldn't choose pixelformat for offscreen rendering\n");
        goto fail;
    }
    SetPixelFormat(osgl->dc, pf, &pfd);

    legacy_context = wglCreateContext(osgl->dc);
    if (!legacy_context || !wglMakeCurrent(osgl->dc, legacy_context)) {
        MP_FATAL(ctx->vo, "Couldn't create GL context for offscreen rendering\n");
        goto fail;
    }

    const char *(GLAPIENTRY *wglGetExtensionsStringARB)(HDC hdc)
        = w32gpa((const GLubyte*)"wglGetExtensionsStringARB");
    if (!wglGetExtensionsStringARB) {
        MP_FATAL(ctx->vo, "The OpenGL driver does not support OpenGL 3.x\n");
        goto fail;
    }

    const char *wgl_exts = wglGetExtensionsStringARB(osgl->dc);
    if (!strstr(wgl_exts, "WGL_ARB_create_context")) {
        MP_FATAL(ctx->vo, "The OpenGL driver does not support OpenGL 3.x\n");
        goto fail;
    }

    HGLRC (GLAPIENTRY *wglCreateContextAttribsARB)(HDC hDC, HGLRC hShareContext,
                                                   const int *attribList)
        = w32gpa((const GLubyte*)"wglCreateContextAttribsARB");
    if (!wglCreateContextAttribsARB) {
        MP_FATAL(ctx->vo, "The OpenGL driver does not support OpenGL 3.x\n");
        goto fail;
    }

    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 0,
        WGL_CONTEXT_FLAGS_ARB, 0,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    osgl->ctx = wglCreateContextAttribsARB(osgl->dc, 0, attribs);
    if (!osgl->ctx) {
        // NVidia, instead of ignoring WGL_CONTEXT_FLAGS_ARB, will error out if
        // it's present on pre-3.2 contexts.
        // Remove it from attribs and retry the context creation.
        attribs[6] = attribs[7] = 0;
        osgl->ctx = wglCreateContextAttribsARB(osgl->dc, 0, attribs);
    }
    if (!osgl->ctx) {
        MP_FATAL(ctx->vo, "Couldn't create GL 3.x context for offscreen rendering\n");
        goto fail;
    }

    wglMakeCurrent(osgl->dc, NULL);
    wglDeleteContext(legacy_context);
    legacy_context = NULL;

    if (!wglMakeCurrent(osgl->dc, osgl->ctx)) {
        MP_FATAL(ctx->vo, "Couldn't create GL 3.x context for offscreen rendering\n");
        goto fail;
    }

    mpgl_load_functions(ctx->gl, w32gpa, wgl_exts, ctx->vo->log);

    return 0;
fail:
    if (legacy_context) {
        wglMakeCurrent(osgl->dc, NULL);
        wglDeleteContext(legacy_context);
    }
    return -1;
}

void mp_dxinterop_os_gl_destroy(struct MPGLContext *ctx,
                                struct offscreen_gl *osgl)
{
    if (osgl->ctx) {
        wglMakeCurrent(osgl->dc, NULL);
        wglDeleteContext(osgl->ctx);
    }
    if (osgl->dc)
        ReleaseDC(osgl->wnd, osgl->dc);
    if (osgl->wnd)
        DestroyWindow(osgl->wnd);
}
