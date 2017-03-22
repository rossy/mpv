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

#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <X11/Xlib.h>
#include <GL/glx.h>
#include <libavutil/common.h>

#define MP_GET_GLX_WORKAROUNDS
#include "header_fixes.h"

#include "osdep/timer.h"
#include "video/out/x11_common.h"
#include "context.h"

enum ust_type {
    UST_TYPE_DONTKNOW,
    UST_TYPE_MONOTONIC,
    UST_TYPE_REALTIME,
    UST_TYPE_OTHER,
};

struct glx_context {
    XVisualInfo *vinfo;
    GLXContext context;
    GLXFBConfig fbc;

    // GLX_OML_sync_control state
    bool use_glx_oml_sync_control;
    int64_t last_sbc;
    enum ust_type ust_type;
};

static void glx_uninit(MPGLContext *ctx)
{
    struct glx_context *glx_ctx = ctx->priv;
    if (glx_ctx->vinfo)
        XFree(glx_ctx->vinfo);
    if (glx_ctx->context) {
        Display *display = ctx->vo->x11->display;
        glXMakeCurrent(display, None, NULL);
        glXDestroyContext(display, glx_ctx->context);
    }
    vo_x11_uninit(ctx->vo);
}

static bool create_context_x11_old(struct MPGLContext *ctx)
{
    struct glx_context *glx_ctx = ctx->priv;
    Display *display = ctx->vo->x11->display;
    struct vo *vo = ctx->vo;
    GL *gl = ctx->gl;

    if (glx_ctx->context)
        return true;

    if (!glx_ctx->vinfo) {
        MP_FATAL(vo, "Can't create a legacy GLX context without X visual\n");
        return false;
    }

    GLXContext new_context = glXCreateContext(display, glx_ctx->vinfo, NULL,
                                              True);
    if (!new_context) {
        MP_FATAL(vo, "Could not create GLX context!\n");
        return false;
    }

    if (!glXMakeCurrent(display, ctx->vo->x11->window, new_context)) {
        MP_FATAL(vo, "Could not set GLX context!\n");
        glXDestroyContext(display, new_context);
        return false;
    }

    const char *glxstr = glXQueryExtensionsString(display, ctx->vo->x11->screen);

    mpgl_load_functions(gl, (void *)glXGetProcAddressARB, glxstr, vo->log);

    glx_ctx->context = new_context;

    return true;
}

typedef GLXContext (*glXCreateContextAttribsARBProc)
    (Display*, GLXFBConfig, GLXContext, Bool, const int*);

static bool create_context_x11_gl3(struct MPGLContext *ctx, int vo_flags,
                                   int gl_version, bool es)
{
    struct glx_context *glx_ctx = ctx->priv;
    struct vo *vo = ctx->vo;

    if (glx_ctx->context)
        return true;

    glXCreateContextAttribsARBProc glXCreateContextAttribsARB =
        (glXCreateContextAttribsARBProc)
            glXGetProcAddressARB((const GLubyte *)"glXCreateContextAttribsARB");

    const char *glxstr =
        glXQueryExtensionsString(vo->x11->display, vo->x11->screen);
    bool have_ctx_ext = glxstr && !!strstr(glxstr, "GLX_ARB_create_context");

    if (!(have_ctx_ext && glXCreateContextAttribsARB)) {
        return false;
    }

    int ctx_flags = vo_flags & VOFLAG_GL_DEBUG ? GLX_CONTEXT_DEBUG_BIT_ARB : 0;
    int profile_mask = GLX_CONTEXT_CORE_PROFILE_BIT_ARB;

    if (es) {
        profile_mask = GLX_CONTEXT_ES2_PROFILE_BIT_EXT;
        if (!(glxstr && strstr(glxstr, "GLX_EXT_create_context_es2_profile")))
            return false;
    }

    int context_attribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, MPGL_VER_GET_MAJOR(gl_version),
        GLX_CONTEXT_MINOR_VERSION_ARB, MPGL_VER_GET_MINOR(gl_version),
        GLX_CONTEXT_PROFILE_MASK_ARB, profile_mask,
        GLX_CONTEXT_FLAGS_ARB, ctx_flags,
        None
    };
    vo_x11_silence_xlib(1);
    GLXContext context = glXCreateContextAttribsARB(vo->x11->display,
                                                    glx_ctx->fbc, 0, True,
                                                    context_attribs);
    vo_x11_silence_xlib(-1);
    if (!context)
        return false;

    // set context
    if (!glXMakeCurrent(vo->x11->display, vo->x11->window, context)) {
        MP_FATAL(vo, "Could not set GLX context!\n");
        glXDestroyContext(vo->x11->display, context);
        return false;
    }

    glx_ctx->context = context;

    mpgl_load_functions(ctx->gl, (void *)glXGetProcAddressARB, glxstr, vo->log);

    return true;
}

// The GL3/FBC initialization code roughly follows/copies from:
//  http://www.opengl.org/wiki/Tutorial:_OpenGL_3.0_Context_Creation_(GLX)
// but also uses some of the old code.

static GLXFBConfig select_fb_config(struct vo *vo, const int *attribs, int flags)
{
    int fbcount;
    GLXFBConfig *fbc = glXChooseFBConfig(vo->x11->display, vo->x11->screen,
                                         attribs, &fbcount);
    if (!fbc)
        return NULL;

    // The list in fbc is sorted (so that the first element is the best).
    GLXFBConfig fbconfig = fbcount > 0 ? fbc[0] : NULL;

    if (flags & VOFLAG_ALPHA) {
        for (int n = 0; n < fbcount; n++) {
            XVisualInfo *v = glXGetVisualFromFBConfig(vo->x11->display, fbc[n]);
            if (v) {
                bool is_rgba = vo_x11_is_rgba_visual(v);
                XFree(v);
                if (is_rgba) {
                    fbconfig = fbc[n];
                    break;
                }
            }
        }
    }

    XFree(fbc);

    return fbconfig;
}

static void set_glx_attrib(int *attribs, int name, int value)
{
    for (int n = 0; attribs[n * 2 + 0] != None; n++) {
        if (attribs[n * 2 + 0] == name) {
            attribs[n * 2 + 1] = value;
            break;
        }
    }
}

static int glx_init(struct MPGLContext *ctx, int flags)
{
    struct vo *vo = ctx->vo;
    struct glx_context *glx_ctx = ctx->priv;

    if (!vo_x11_init(ctx->vo))
        goto uninit;

    int glx_major, glx_minor;

    if (!glXQueryVersion(vo->x11->display, &glx_major, &glx_minor)) {
        MP_ERR(vo, "GLX not found.\n");
        goto uninit;
    }
    // FBConfigs were added in GLX version 1.3.
    if (MPGL_VER(glx_major, glx_minor) <  MPGL_VER(1, 3)) {
        MP_ERR(vo, "GLX version older than 1.3.\n");
        goto uninit;
    }

    int glx_attribs[] = {
        GLX_X_RENDERABLE, True,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 1,
        GLX_GREEN_SIZE, 1,
        GLX_BLUE_SIZE, 1,
        GLX_ALPHA_SIZE, 0,
        GLX_DOUBLEBUFFER, True,
        None
    };
    GLXFBConfig fbc = NULL;
    if (flags & VOFLAG_ALPHA) {
        set_glx_attrib(glx_attribs, GLX_ALPHA_SIZE, 1);
        fbc = select_fb_config(vo, glx_attribs, flags);
        if (!fbc) {
            set_glx_attrib(glx_attribs, GLX_ALPHA_SIZE, 0);
            flags &= ~VOFLAG_ALPHA;
        }
    }
    if (!fbc)
        fbc = select_fb_config(vo, glx_attribs, flags);
    if (!fbc) {
        MP_ERR(vo, "no GLX support present\n");
        goto uninit;
    }

    int fbid = -1;
    if (!glXGetFBConfigAttrib(vo->x11->display, fbc, GLX_FBCONFIG_ID, &fbid))
        MP_VERBOSE(vo, "GLX chose FB config with ID 0x%x\n", fbid);

    glx_ctx->fbc = fbc;
    glx_ctx->vinfo = glXGetVisualFromFBConfig(vo->x11->display, fbc);
    if (glx_ctx->vinfo) {
        MP_VERBOSE(vo, "GLX chose visual with ID 0x%x\n",
                   (int)glx_ctx->vinfo->visualid);
    } else {
        MP_WARN(vo, "Selected GLX FB config has no associated X visual\n");
    }

    if (!vo_x11_create_vo_window(vo, glx_ctx->vinfo, "gl"))
        goto uninit;

    bool success = false;
    if (!(flags & VOFLAG_GLES)) {
        for (int n = 0; mpgl_preferred_gl_versions[n]; n++) {
            int version = mpgl_preferred_gl_versions[n];
            MP_VERBOSE(vo, "Creating OpenGL %d.%d context...\n",
                       MPGL_VER_P(version));
            if (version >= 300) {
                success = create_context_x11_gl3(ctx, flags, version, false);
            } else {
                success = create_context_x11_old(ctx);
            }
            if (success)
                break;
        }
    }
    if (!success) // try ES
        success = create_context_x11_gl3(ctx, flags, 200, true);
    if (success && !glXIsDirect(vo->x11->display, glx_ctx->context))
        ctx->gl->mpgl_caps |= MPGL_CAP_SW;
    if (!success)
        goto uninit;

    glx_ctx->use_glx_oml_sync_control = ctx->gl->SwapBuffersMsc != NULL;

    return 0;

uninit:
    glx_uninit(ctx);
    return -1;
}

static int glx_init_probe(struct MPGLContext *ctx, int flags)
{
    int r = glx_init(ctx, flags);
    if (r >= 0) {
        if (!(ctx->gl->mpgl_caps & MPGL_CAP_VDPAU)) {
            MP_VERBOSE(ctx->vo, "No vdpau support found - probing more things.\n");
            glx_uninit(ctx);
            r = -1;
        }
    }
    return r;
}

static int glx_reconfig(struct MPGLContext *ctx)
{
    vo_x11_config_vo_window(ctx->vo);
    return 0;
}

static int glx_control(struct MPGLContext *ctx, int *events, int request,
                       void *arg)
{
    return vo_x11_control(ctx->vo, events, request, arg);
}

static void glx_swap_buffers(struct MPGLContext *ctx)
{
    struct glx_context *glx_ctx = ctx->priv;
    struct vo *vo = ctx->vo;
    GL *gl = ctx->gl;

    if (glx_ctx->use_glx_oml_sync_control) {
        // Get the UST/MSC/SBC pair for the last frame that was made visible.
        // This is only possible if mpv has presented at least one frame. On
        // the first frame, glx_ctx->last_sbc is 0 and target_msc will be set
        // to 1, so the frame will be presented as soon as possible.
        int64_t vis_ust = 0, vis_msc = 0, vis_sbc = 0;
        if (glx_ctx->last_sbc > 0) {
            gl->WaitForSbc(vo->x11->display, vo->x11->window, 1,
                           &vis_ust, &vis_msc, &vis_sbc);
        }

        // Schedule the next frame after any pending frames. This seems to be
        // the same logic that Mesa uses for scheduling frames presented with
        // glXSwapBuffers using DRI3/Present, but unlike glXSwapBuffers,
        // glxSwapBuffersMsc does not perform an implicit glFlush.
        int64_t pending_frames = FFMAX(glx_ctx->last_sbc - vis_sbc, 0);
        int64_t target_msc = vis_msc + pending_frames + 1;
        glx_ctx->last_sbc = gl->SwapBuffersMsc(vo->x11->display,
                                               vo->x11->window,
                                               target_msc, 0, 0);
    } else {
        glXSwapBuffers(vo->x11->display, vo->x11->window);
    }
}

static int64_t get_realtime_clock(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ll + tv.tv_usec;
}

#if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0 && defined(CLOCK_MONOTONIC)
static int64_t get_monotonic_clock(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return INT64_MIN;
    return ts.tv_sec * 1000000ll + ts.tv_nsec / 1000ll;
}
#else
static int64_t get_monotonic_clock(void)
{
    return INT64_MIN;
}
#endif

static int64_t get_ust(enum ust_type type)
{
    if (type == UST_TYPE_MONOTONIC)
        return get_monotonic_clock();
    if (type == UST_TYPE_REALTIME)
        return get_realtime_clock();
    return INT64_MIN;
}

static enum ust_type guess_ust_type(int64_t ust)
{
    // The GLX_OML_sync_control extension does not specify which system clock
    // UST timestamps refer to, and on Linux/Mesa it's hard to tell. Apparently
    // kernel versions before 3.8 use the realtime clock and later versions use
    // the monotonic clock, so we need to guess which one it really is.
    if (llabs(ust - get_ust(UST_TYPE_MONOTONIC)) < 10000000ll)
        return UST_TYPE_MONOTONIC;
    if (llabs(ust - get_ust(UST_TYPE_REALTIME)) < 10000000ll)
        return UST_TYPE_REALTIME;
    return UST_TYPE_OTHER;
}

static void glx_get_frame_statistics(struct MPGLContext *ctx,
                                     struct vo_frame_statistics *st)
{
    struct glx_context *glx_ctx = ctx->priv;
    struct vo *vo = ctx->vo;
    GL *gl = ctx->gl;

    if (!glx_ctx->use_glx_oml_sync_control)
        return;

    // glXGetMscRateOML is potentially more useful than just using XRandR
    // because it doesn't require guessing which monitor to synchronize to on a
    // multi-monitor system
    int32_t rate_num = 0, rate_denom = 0;
    int64_t rate_us = 0;
    if (gl->GetMscRate(vo->x11->display, vo->x11->window,
                       &rate_num, &rate_denom))
    {
        // Apparently glXGetMscRateOML doesn't work on some Mesa systems, so be
        // paranoid and check that rate_num and rate_denom were set
        if (rate_num > 0 && rate_denom > 0)
            rate_us = 1000000ll * rate_denom / rate_num;
    }

    // Get the current UST/MSC/SBC triple. This should request the data for the
    // most recent VBlank event from the X server, even if mpv didn't present a
    // new frame on that event.
    int64_t sync_ust = 0, sync_msc = 0, sync_sbc = 0;
    gl->GetSyncValues(vo->x11->display, vo->x11->window,
                      &sync_ust, &sync_msc, &sync_sbc);

    // If this is the first UST timestamp we've received, guess which system
    // clock it refers to. See guess_ust_type() for more info.
    if (sync_ust && glx_ctx->ust_type == UST_TYPE_DONTKNOW) {
        glx_ctx->ust_type = guess_ust_type(sync_ust);
        switch (glx_ctx->ust_type) {
        case UST_TYPE_MONOTONIC:
            MP_VERBOSE(ctx, "UST timestamps are on the monotonic clock\n");
            break;
        case UST_TYPE_REALTIME:
            MP_VERBOSE(ctx, "UST timestamps are on the realtime clock\n");
            break;
        default:
            MP_VERBOSE(ctx, "UST timestamps are not usable\n");
        }
    }
    if (glx_ctx->ust_type == UST_TYPE_OTHER)
        return;

    // glXWaitForSbcOML should get a UST/MSC/SBC triple for the last VBlank
    // event at which mpv presented an actual frame. This might be different to
    // the triple returned by glXGetSyncValuesOML if mpv didn't present a frame
    // on the most recent VBlank event.
    int64_t vis_ust = 0, vis_msc = 0, vis_sbc = 0;
    if (sync_sbc) {
        gl->WaitForSbc(vo->x11->display, vo->x11->window, sync_sbc,
                       &vis_ust, &vis_msc, &vis_sbc);
    }

    int64_t cur_ust = get_ust(glx_ctx->ust_type);
    int64_t cur_mp_time = mp_time_us();

    st->most_recent_frame_id = glx_ctx->last_sbc;
    st->hw_visible_frame_id = vis_sbc;
    st->hw_visible_frame_time_us = vis_ust - cur_ust + cur_mp_time;
    st->hw_visible_frame_vsync_count = vis_msc;
    st->hw_last_vsync_count = sync_msc;
    st->hw_last_vsync_time_us = sync_ust - cur_ust + cur_mp_time;
    st->nominal_vsync_duration_us = rate_us;
}

static void glx_wakeup(struct MPGLContext *ctx)
{
    vo_x11_wakeup(ctx->vo);
}

static void glx_wait_events(struct MPGLContext *ctx, int64_t until_time_us)
{
    vo_x11_wait_events(ctx->vo, until_time_us);
}

const struct mpgl_driver mpgl_driver_x11 = {
    .name                 = "x11",
    .priv_size            = sizeof(struct glx_context),
    .init                 = glx_init,
    .reconfig             = glx_reconfig,
    .swap_buffers         = glx_swap_buffers,
    .get_frame_statistics = glx_get_frame_statistics,
    .control              = glx_control,
    .wakeup               = glx_wakeup,
    .wait_events          = glx_wait_events,
    .uninit               = glx_uninit,
};

const struct mpgl_driver mpgl_driver_x11_probe = {
    .name                 = "x11probe",
    .priv_size            = sizeof(struct glx_context),
    .init                 = glx_init_probe,
    .reconfig             = glx_reconfig,
    .swap_buffers         = glx_swap_buffers,
    .get_frame_statistics = glx_get_frame_statistics,
    .control              = glx_control,
    .wakeup               = glx_wakeup,
    .wait_events          = glx_wait_events,
    .uninit               = glx_uninit,
};
