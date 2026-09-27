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

#if !defined (ZV_ACCEPTOR_H)
#define ZV_ACCEPTOR_H

#include "zv/export.h"
#include "zv/os_socket.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct zv_loop_s zv_loop_t;
typedef struct zv_acceptor_s zv_acceptor_t;

ZV_EXPORT zv_acceptor_t * zv_acceptor_create(
    zv_loop_t * loop,
    const struct sockaddr * addr,
    socklen_t addrlen,
    void (*accept_handler)(zv_acceptor_t*, zv_socket_t sockfd),
    void * extra_data
);

ZV_EXPORT zv_loop_t * zv_acceptor_get_loop(zv_acceptor_t * acceptor);
ZV_EXPORT in_port_t zv_acceptor_get_port(zv_acceptor_t * acceptor);
ZV_EXPORT void * zv_acceptor_get_extra_data(zv_acceptor_t * acceptor);

ZV_EXPORT int zv_acceptor_start(zv_acceptor_t * acceptor);

ZV_EXPORT int zv_acceptor_stop(
    zv_acceptor_t * acceptor,
    void (*after_stop)(zv_acceptor_t*, void*),
    void * after_stop_arg
);

ZV_EXPORT void zv_acceptor_destroy(zv_acceptor_t * acceptor);

enum zv_log_level;
ZV_EXPORT void zv_acceptor_set_log_level(enum zv_log_level level);

#if defined(__cplusplus)
}
#endif

#endif /* !defined(ZV_ACCEPTOR_H) */
