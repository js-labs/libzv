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

#if !defined (ZV_HE_CONNECTOR_H)
#define ZV_HE_CONNECTOR_H

#include "zv/export.h"
#include "zv/os_socket.h"

typedef struct zv_loop_s zv_loop_t;
typedef struct zv_he_connector_s zv_he_connector_t;

ZV_EXPORT zv_he_connector_t * zv_he_connector_create(
    zv_loop_t * loop,
    const char * host,
    const char * service,
    int sock_type,
    void (*after_connect)(zv_he_connector_t*, zv_socket_t),
    void (*after_fail)(zv_he_connector_t*, /*errcode*/int),
    void * extra_data
);

ZV_EXPORT zv_loop_t * zv_he_connector_get_loop(zv_he_connector_t * he_connector);
ZV_EXPORT const char * zv_he_connector_get_host(zv_he_connector_t * he_connector);
ZV_EXPORT const char * zv_he_connector_get_service(zv_he_connector_t * he_connector);
ZV_EXPORT void * zv_he_connector_get_extra_data(zv_he_connector_t * he_connector);

ZV_EXPORT zv_he_connector_t * zv_he_connector_retain(zv_he_connector_t * he_connector);
ZV_EXPORT void zv_he_connector_release(zv_he_connector_t * he_connector);

ZV_EXPORT int zv_he_connector_stop(zv_he_connector_t * he_connector);

enum zv_log_level;
ZV_EXPORT void zv_he_connector_set_log_level(enum zv_log_level level);

#endif /* !defined(ZV_HE_CONNECTOR_H) */
