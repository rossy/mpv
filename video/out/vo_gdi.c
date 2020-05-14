/*
 * This file is part of mpv video player.
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

#include "sub/osd.h"
#include "video/out/w32_common.h"
#include "video/sws_utils.h"
#include "vo.h"

struct priv {
    struct mp_sws_context *sws;
    struct mp_rect src;
    struct mp_rect dst;
    struct mp_osd_res osd;
    HDC dc;
    HDC dibdc;

    HBITMAP fb_dib;
    struct mp_image fb_mpi;
};

static void uninit(struct vo *vo)
{
    struct priv *p = vo->priv;

    if (p->fb_dib)
        DeleteObject(p->fb_dib);
    if (p->dibdc)
        DeleteDC(p->dibdc);
    if (p->dc)
        ReleaseDC(vo_w32_hwnd(vo), p->dc);
    vo_w32_uninit(vo);
}

static int preinit(struct vo *vo)
{
    struct priv *p = vo->priv;

    p->sws = mp_sws_alloc(vo);
    p->sws->log = vo->log;
    mp_sws_enable_cmdline_opts(p->sws, vo->global);

    if (!vo_w32_init(vo))
        goto error;

    p->dc = GetDC(vo_w32_hwnd(vo));
    if (!p->dc)
        goto error;

    p->dibdc = CreateCompatibleDC(p->dc);
    if (!p->dibdc)
        goto error;

    return 0;

error:
    uninit(vo);
    return 1;
}

static int query_format(struct vo *vo, int format)
{
    struct priv *p = vo->priv;
    if (mp_sws_supports_formats(p->sws, IMGFMT_RGB0, format))
        return 1;
    return 0;
}

static int reconfig(struct vo *vo, struct mp_image_params *params)
{
    struct priv *p = vo->priv;
    vo_w32_config(vo);
    p->sws->src = *params;
    return 0;
}

static int resize(struct vo *vo)
{
    struct priv *p = vo->priv;

    p->sws->dst = (struct mp_image_params) {
        .imgfmt = IMGFMT_BGR0,
        .w = vo->dwidth,
        .h = vo->dheight,
        .p_w = 1,
        .p_h = 1,
    };
    mp_image_params_guess_csp(&p->sws->dst);

    p->fb_mpi = (struct mp_image){
        .w = vo->dwidth,
        .h = vo->dheight,
        .stride[0] = vo->dwidth * 4,
    };
    mp_image_set_params(&p->fb_mpi, &p->sws->dst);

    if (p->fb_dib)
        DeleteObject(p->fb_dib);
    BITMAPINFO info = {
        .bmiHeader = {
            .biSize = sizeof(BITMAPINFO),
            .biWidth = vo->dwidth,
            .biHeight = -vo->dheight,
            .biPlanes = 1,
            .biBitCount = 32,
            .biCompression = BI_RGB,
        },
    };
    p->fb_dib = CreateDIBSection(p->dibdc, &info, DIB_RGB_COLORS,
                                 (void **)&p->fb_mpi.planes[0], NULL, 0);
    if (!p->fb_dib)
        return 0;

    SelectObject(p->dibdc, p->fb_dib);

    return mp_sws_reinit(p->sws);
}

static int control(struct vo *vo, uint32_t request, void *data)
{
    int events = 0;
    int ret = vo_w32_control(vo, &events, request, data);

    if (events & VO_EVENT_RESIZE)
        ret = resize(vo);
    vo_event(vo, events);
    return ret;
}

static void draw_image(struct vo *vo, mp_image_t *mpi)
{
    struct priv *p = vo->priv;
    mp_sws_scale(p->sws, &p->fb_mpi, mpi);
    talloc_free(mpi);
}

static void flip_page(struct vo *vo)
{
    struct priv *p = vo->priv;
    BitBlt(p->dc, 0, 0, vo->dwidth, vo->dheight, p->dibdc, 0, 0, SRCCOPY);
}

const struct vo_driver video_out_gdi = {
    .description = "GDI software renderer",
    .name = "gdi",
    .priv_size = sizeof(struct priv),
    .preinit = preinit,
    .query_format = query_format,
    .reconfig = reconfig,
    .control = control,
    .draw_image = draw_image,
    .flip_page = flip_page,
    .uninit = uninit,
};
