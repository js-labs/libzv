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

#if !defined (ZV_CONNECTOR_H)
#define ZV_CONNECTOR_H

#include "zv/export.h"
#include "zv/os_socket.h"

typedef struct zv_loop_s zv_loop_t;
typedef struct zv_connector_s zv_connector_t;

ZV_EXPORT zv_connector_t * zv_connector_create(
    zv_loop_t * loop,
    const struct sockaddr * addr,
    socklen_t addrlen,
    void (*after_connect)(zv_connector_t*, zv_socket_t),
    void (*after_fail)(zv_connector_t*, int /*sockerr*/),
    void * extra_data
);

ZV_EXPORT void zv_connector_destroy(zv_connector_t * connector);

ZV_EXPORT zv_loop_t * zv_connector_get_loop(zv_connector_t * connector);
ZV_EXPORT void * zv_connector_get_extra_data(zv_connector_t * connector);
ZV_EXPORT const struct sockaddr * zv_connector_get_addr(zv_connector_t * connector, socklen_t * addrlen);

ZV_EXPORT void zv_connector_start(zv_connector_t * connector);

ZV_EXPORT void zv_connector_set_log_level(enum zv_log_level level);

#endif /* !defined(ZV_CONNECTOR_H) */
