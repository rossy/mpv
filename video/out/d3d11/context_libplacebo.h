#pragma once

#include "video/out/gpu/context.h"

#include <libplacebo/d3d11.h>

struct d3d11_libplacebo_ctx {
    struct mp_log *pl_log;
    pl_log ctx;
    pl_d3d11 d3d11;
    pl_gpu gpu;
    pl_swapchain swapchain;
    struct ra_tex proxy_tex;
};

// May be called on a ra_ctx of any type.
struct d3d11_libplacebo_ctx *ra_d3d11_libplacebo_ctx_get(struct ra_ctx *ctx);
