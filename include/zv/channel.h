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

#if !defined(ZV_CHANNEL_H)
#define ZV_CHANNEL_H

#include "zv/export.h"
#include "zv/os_socket.h"

typedef struct zv_loop_s zv_loop_t;
typedef struct zv_buffer_cache_s zv_buffer_cache_t;
typedef struct zv_channel_s zv_channel_t;

ZV_EXPORT zv_channel_t * zv_channel_create(
    zv_loop_t * loop,
    zv_buffer_cache_t * buffer_cache,
    zv_socket_t sockfd,
    void (*input_handler)(zv_channel_t*, const void *, size_t),
    void (*close_handler)(zv_channel_t*),
    void * extra_data
);

ZV_EXPORT void zv_channel_start(zv_channel_t * channel);
ZV_EXPORT int zv_channel_stop(zv_channel_t * channel);

ZV_EXPORT zv_channel_t * zv_channel_retain(zv_channel_t * channel);
ZV_EXPORT void zv_channel_release(zv_channel_t * channel);

ZV_EXPORT zv_loop_t * zv_channel_get_loop(zv_channel_t * channel);

ZV_EXPORT void * zv_channel_set_extra_data(zv_channel_t * channel, void * extra_data);
ZV_EXPORT void * zv_channel_get_extra_data(zv_channel_t * channel);

ZV_EXPORT const struct sockaddr * zv_channel_get_local_addr(zv_channel_t * channel, socklen_t * addrlen);
ZV_EXPORT const char * zv_channel_get_local_addr_str(zv_channel_t * channel);

ZV_EXPORT const struct sockaddr * zv_channel_get_peer_addr(zv_channel_t * channel, socklen_t * addrlen);
ZV_EXPORT const char * zv_channel_get_peer_addr_str(zv_channel_t * channel);

ZV_EXPORT int zv_channel_send(zv_channel_t * channel, const void * data, size_t size);
ZV_EXPORT int zv_channel_send_sync(zv_channel_t * channel, const void * data, size_t size);

enum zv_log_level;
ZV_EXPORT void zv_channel_set_log_level(enum zv_log_level level);

#endif /* !defined(ZV_CHANNEL_H) */
