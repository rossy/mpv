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

#include <pthread.h>

#include "config.h"

#include "common/common.h"
#include "common/msg.h"
#include "common/msg_control.h"

#include "subprocess.h"

void mp_devnull(void *ctx, char *data, size_t size)
{
}

int mp_subprocess(char **args, struct mp_cancel *cancel, void *ctx,
                  subprocess_read_cb on_stdout, subprocess_read_cb on_stderr,
                  char **error)
{
    struct mp_subprocess_opts opts = {
        .exe = args[0],
        .args = args,
        .cancel = cancel,
    };
    opts.fds[opts.num_fds++] = (struct mp_subprocess_fd){
        .fd = 0, // stdin
        .src_fd = 0,
    };
    opts.fds[opts.num_fds++] = (struct mp_subprocess_fd){
        .fd = 1, // stdout
        .on_read = on_stdout,
        .on_read_ctx = ctx,
        .src_fd = on_stdout ? -1 : 1,
    };
    opts.fds[opts.num_fds++] = (struct mp_subprocess_fd){
        .fd = 2, // stderr
        .on_read = on_stderr,
        .on_read_ctx = ctx,
        .src_fd = on_stderr ? -1 : 2,
    };
    struct mp_subprocess_result res;
    mp_subprocess2(&opts, &res);
    if (res.error < 0) {
        *error = (char *)mp_subprocess_err_str(res.error);
        return res.error;
    }
    return res.exit_status;
}

void mp_subprocess_detached(struct mp_log *log, char **args)
{
    mp_msg_flush_status_line(log);

    struct mp_subprocess_opts opts = {
        .exe = args[0],
        .args = args,
        .fds = {
            {.fd = 0, .src_fd = 0,},
            {.fd = 1, .src_fd = 1,},
            {.fd = 2, .src_fd = 2,},
        },
        .num_fds = 3,
        .detach = true,
    };
    struct mp_subprocess_result res;
    mp_subprocess2(&opts, &res);
    if (res.error < 0) {
        mp_err(log, "Starting subprocess failed: %s\n",
               mp_subprocess_err_str(res.error));
    }
}

const char *mp_subprocess_err_str(int num)
{
    // Note: these are visible to the public client API
    switch (num) {
    case MP_SUBPROCESS_OK:              return "success";
    case MP_SUBPROCESS_EKILLED_BY_US:   return "killed";
    case MP_SUBPROCESS_EINIT:           return "init";
    case MP_SUBPROCESS_EUNSUPPORTED:    return "unsupported";
    case MP_SUBPROCESS_EGENERIC:        // fall through
    default:                            return "unknown";
    }
}
