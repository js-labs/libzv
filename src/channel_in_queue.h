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

#if !defined(ZV_CHANNEL_IN_QUEUE_H)
#define ZV_CHANNEL_IN_QUEUE_H

#include <stddef.h>
#include "zv/config.h"

#if defined(_WIN32)
struct _WSABUF;
#if !defined(iovec)
#define iovec _WSABUF
#endif
#else
struct iovec;
#endif

#if defined(__INTELLISENSE__)
#define _Atomic
#endif

typedef struct zv_buffer_cache_s zv_buffer_cache_t;
typedef struct zv_channel_s zv_channel_t;
struct zv_buffer_s;

struct zv_channel_in_queue_s
{
    struct zv_channel_in_queue_config_s config;
    zv_buffer_cache_t * buffer_cache;

    struct zv_buffer_s * head;
    size_t rd_offs;

    struct zv_buffer_s * tail;
    size_t wr_offs;

    _Atomic size_t state;
};

void zv_channel_in_queue_init(
    struct zv_channel_in_queue_s * in_queue,
    const struct zv_channel_in_queue_config_s * config,
    zv_buffer_cache_t * buffer_cache
);

void zv_channel_in_queue_deinit(struct zv_channel_in_queue_s * in_queue);

void zv_channel_in_queue_get_wr(
    struct zv_channel_in_queue_s * in_queue,
    struct iovec * iov
);

size_t zv_channel_in_queue_move_wr(
    struct zv_channel_in_queue_s * in_queue,
    size_t bytes,
    int * psuspend
);

size_t zv_channel_in_queue_process_data(
    struct zv_channel_in_queue_s * in_queue,
    size_t bytes_ready,
    void (*input_handler)(zv_channel_t*, const void*, size_t),
    zv_channel_t * channel,
    int * presume_action
);

// returns no-zero if empty
int zv_channel_in_queue_close(
    struct zv_channel_in_queue_s * in_queue,
    int * is_stopped
);

int zv_channel_in_queue_stop(
    struct zv_channel_in_queue_s * in_queue
);

#endif /* !defined(ZV_CHANNEL_IN_QUEUE_H) */
