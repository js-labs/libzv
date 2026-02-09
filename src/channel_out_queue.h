/*
 * Copyright (C) 2022 Sergey Zubarev, info@js-labs.org
 *
 * This file is a part of UIO framework.
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

#if !defined(ZV_CHANEL_OUT_QUEUE_H)
#define ZV_CHANEL_OUT_QUEUE_H

#include <stddef.h>

#if defined(_WIN32)
#include <WinSock2.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/uio.h>
#endif

typedef struct zv_buffer_cache_s zv_buffer_cache_t;
struct zv_buffer_s;

struct zv_channel_out_queue_s
{
    zv_buffer_cache_t * buffer_cache;

    struct zv_buffer_s * head;
    struct zv_buffer_s * tail;

    size_t rd_offs;
    size_t add_state;
    size_t state;
};

void zv_channel_out_queue_init(
    struct zv_channel_out_queue_s * out_queue,
    zv_buffer_cache_t * buffer_cache
);

void zv_channel_out_queue_deinit(
    struct zv_channel_out_queue_s * out_queue
);

int zv_channel_out_queue_add_data(
    struct zv_channel_out_queue_s * out_queue,
    const void * data,
    size_t size
);

int zv_channel_out_queue_get_data(
    struct zv_channel_out_queue_s * out_queue,
#if defined(_WIN32)
    WSABUF * iov,
#else
    struct iovec * iov,
#endif
    int iovc_max
);

size_t zv_channel_out_queue_remove_data(
    struct zv_channel_out_queue_s * out_queue,
    size_t bytes
);

int zv_channel_out_queue_close(
    struct zv_channel_out_queue_s * out_queue,
    int * is_empty
);

#endif /* !defined(ZV_CHANEL_OUT_QUEUE_H) */
