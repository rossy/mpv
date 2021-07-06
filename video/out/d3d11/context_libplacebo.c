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

#include "common/msg.h"
#include "options/m_config.h"
#include "osdep/timer.h"
#include "osdep/windows_utils.h"

#include "video/out/gpu/context.h"
#include "video/out/placebo/ra_pl.h"
#include "video/out/placebo/utils.h"
#include "video/out/w32_common.h"

#include "context_libplacebo.h"

// static int d3d11_validate_adapter(struct mp_log *log,
//                                   const struct m_option *opt,
//                                   struct bstr name, const char **value);

// struct d3d11_opts {
//     int feature_level;
//     int warp;
//     int flip;
//     char *adapter_name;
// };

// #define OPT_BASE_STRUCT struct d3d11_opts
// const struct m_sub_options d3d11_conf = {
//     .opts = (const struct m_option[]) {
//         {"d3d11-warp", OPT_CHOICE(warp,
//             {"auto", -1},
//             {"no", 0},
//             {"yes", 1})},
//         {"d3d11-feature-level", OPT_CHOICE(feature_level,
//             {"12_1", D3D_FEATURE_LEVEL_12_1},
//             {"12_0", D3D_FEATURE_LEVEL_12_0},
//             {"11_1", D3D_FEATURE_LEVEL_11_1},
//             {"11_0", D3D_FEATURE_LEVEL_11_0},
//             {"10_1", D3D_FEATURE_LEVEL_10_1},
//             {"10_0", D3D_FEATURE_LEVEL_10_0},
//             {"9_3", D3D_FEATURE_LEVEL_9_3},
//             {"9_2", D3D_FEATURE_LEVEL_9_2},
//             {"9_1", D3D_FEATURE_LEVEL_9_1})},
//         {"d3d11-flip", OPT_FLAG(flip)},
//         {"d3d11-adapter", OPT_STRING_VALIDATE(adapter_name,
//                                               d3d11_validate_adapter)},
//         {0}
//     },
//     .defaults = &(const struct d3d11_opts) {
//         .feature_level = D3D_FEATURE_LEVEL_12_1,
//         .warp = -1,
//         .flip = 1,
//         .adapter_name = NULL,
//     },
//     .size = sizeof(struct d3d11_opts)
// };

// static int d3d11_validate_adapter(struct mp_log *log,
//                                   const struct m_option *opt,
//                                   struct bstr name, const char **value)
// {
//     struct bstr param = bstr0(*value);
//     bool help = bstr_equals0(param, "help");
//     bool adapter_matched = false;
//     struct bstr listing = { 0 };

//     if (bstr_equals0(param, "")) {
//         return 0;
//     }

//     adapter_matched = mp_d3d11_list_or_verify_adapters(log,
//                                                        help ? bstr0(NULL) : param,
//                                                        help ? &listing : NULL);

//     if (help) {
//         mp_info(log, "Available D3D11 adapters:\n%.*s",
//                 BSTR_P(listing));
//         talloc_free(listing.start);
//         return M_OPT_EXIT;
//     }

//     if (!adapter_matched) {
//         mp_err(log, "No adapter matching '%.*s'!\n", BSTR_P(param));
//     }

//     return adapter_matched ? 0 : M_OPT_INVALID;
// }

static bool resize(struct ra_ctx *ctx)
{
    struct d3d11_libplacebo_ctx *p = ctx->swapchain->priv;
    return pl_swapchain_resize(p->swapchain, &ctx->vo->dwidth,
                                             &ctx->vo->dheight);
}

static bool d3d11_reconfig(struct ra_ctx *ctx)
{
    vo_w32_config(ctx->vo);
    return resize(ctx);
}

static int d3d11_color_depth(struct ra_swapchain *sw)
{
    return 0;
}

static bool d3d11_start_frame(struct ra_swapchain *sw, struct ra_fbo *out_fbo)
{
    struct d3d11_libplacebo_ctx *p = sw->priv;
    struct pl_swapchain_frame frame;
    if (!pl_swapchain_start_frame(p->swapchain, &frame))
        return false;
    if (!mppl_wrap_tex(sw->ctx->ra, frame.fbo, &p->proxy_tex))
        return false;

    *out_fbo = (struct ra_fbo) {
        .tex = &p->proxy_tex,
        .flip = frame.flipped,
    };

    return true;
}

static bool d3d11_submit_frame(struct ra_swapchain *sw,
                               const struct vo_frame *frame)
{
    struct d3d11_libplacebo_ctx *p = sw->priv;
    return pl_swapchain_submit_frame(p->swapchain);
}

static void d3d11_swap_buffers(struct ra_swapchain *sw)
{
    struct d3d11_libplacebo_ctx *p = sw->priv;
    pl_swapchain_swap_buffers(p->swapchain);
}

static int d3d11_control(struct ra_ctx *ctx, int *events, int request, void *arg)
{
    int ret = -1;

    ret = vo_w32_control(ctx->vo, events, request, arg);

    if (*events & VO_EVENT_RESIZE) {
        if (!resize(ctx))
            return VO_ERROR;
    }
    return ret;
}

static void d3d11_uninit(struct ra_ctx *ctx)
{
    struct d3d11_libplacebo_ctx *p = ctx->priv;

    vo_w32_uninit(ctx->vo);

    if (ctx->ra) {
        pl_gpu_finish(p->gpu);
        ctx->ra->fns->destroy(ctx->ra);
        ctx->ra = NULL;
    }

    pl_swapchain_destroy(&p->swapchain);
	pl_d3d11_destroy(&p->d3d11);
	pl_log_destroy(&p->ctx);
}

static const struct ra_swapchain_fns d3d11_swapchain = {
    .color_depth  = d3d11_color_depth,
    .start_frame  = d3d11_start_frame,
    .submit_frame = d3d11_submit_frame,
    .swap_buffers = d3d11_swap_buffers,
};

static bool d3d11_init(struct ra_ctx *ctx)
{
    struct d3d11_libplacebo_ctx *p = ctx->priv = talloc_zero(ctx, struct d3d11_libplacebo_ctx);
    // p->opts_cache = m_config_cache_alloc(ctx, ctx->global, &d3d11_conf);
    // p->opts = p->opts_cache->opts;

    struct ra_swapchain *sw = ctx->swapchain = talloc_zero(ctx, struct ra_swapchain);
    sw->priv = p;
    sw->ctx = ctx;
    sw->fns = &d3d11_swapchain;

    p->ctx = pl_log_create(PL_API_VER, NULL);
    if (!p->ctx)
        goto error;

    p->pl_log = mp_log_new(ctx, ctx->log, "libplacebo");
    mppl_ctx_set_log(p->ctx, p->pl_log, false);
    mp_verbose(p->pl_log, "Initialized libplacebo v%d\n", PL_API_VER);

    struct pl_d3d11_params params = pl_d3d11_default_params;
    params.debug = ctx->opts.debug;
    // params.allow_software = p->opts->warp != 0;
    // params.force_software = p->opts->warp == 1;
    // params.max_feature_level = p->opts->feature_level;
    params.max_frame_latency = ctx->vo->opts->swapchain_depth;
    p->d3d11 = pl_d3d11_create(p->ctx, &params);
    if (!p->d3d11)
        goto error;

    p->gpu = p->d3d11->gpu;
    ctx->ra = ra_create_pl(p->gpu, ctx->log);
    if (!ctx->ra)
        goto error;

    if (!vo_w32_init(ctx->vo))
        goto error;

    struct pl_d3d11_swapchain_params scparams = {
        .window = vo_w32_hwnd(ctx->vo),
        .width = ctx->vo->dwidth,
        .height = ctx->vo->dheight,
        // .blit = !p->opts->flip,
    };
    p->swapchain = pl_d3d11_create_swapchain(p->d3d11, &scparams);
    if (!p->swapchain)
        goto error;

    return true;

error:
    d3d11_uninit(ctx);
    return false;
}

struct d3d11_libplacebo_ctx *ra_d3d11_libplacebo_ctx_get(struct ra_ctx *ctx)
{
    if (ctx->swapchain->fns != &d3d11_swapchain)
        return NULL;
    return ctx->priv;
}

const struct ra_ctx_fns ra_ctx_d3d11_libplacebo = {
    .type     = "d3d11",
    .name     = "d3d11-libplacebo",
    .reconfig = d3d11_reconfig,
    .control  = d3d11_control,
    .init     = d3d11_init,
    .uninit   = d3d11_uninit,
};
