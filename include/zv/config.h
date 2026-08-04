/*
 * Copyright (C) 2022 Sergey Zubarev, info@js-labs.org
 *
 * This file is a part of ZV framework.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(ZV_CONFIG_H)
#define ZV_CONFIG_H

#include "zv/export.h"
#include "zv/os_time.h"
#include <stddef.h>

enum zv_loop_backend_e
{
    zv_loop_backend_select
#if defined(_WIN32) || defined(__linux__)
    , zv_loop_backend_poll
#endif
};

struct zv_channel_in_queue_config_s
{
    size_t low_watermark;
    size_t high_watermark;
};

struct zv_loop_config_s
{
    enum zv_loop_backend_e loop_backend;

    struct
    {
        size_t threads;
    }
    thread_pool;

    struct
    {
        struct zv_channel_in_queue_config_s in_queue;
        size_t iov_max;
    }
    channel;

    struct
    {
        clockid_t clockid;
    }
    timer_mgr;
};

ZV_EXPORT const char * zv_loop_backend_get_name(enum zv_loop_backend_e loop_backend);
ZV_EXPORT const struct zv_loop_config_s * zv_loop_config_get_default();

#endif /* !defined(ZV_CONFIG_H) */
