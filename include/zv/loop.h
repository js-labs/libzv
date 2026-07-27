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

#if !defined(ZV_LOOP_H)
#define ZV_LOOP_H

#include "zv/export.h"
#include <stddef.h>

typedef struct zv_loop_s zv_loop_t;
typedef struct zv_thread_pool_s zv_thread_pool_t;
typedef struct zv_timer_mgr_s zv_timer_mgr_t;

struct zv_loop_config_s;

ZV_EXPORT zv_loop_t * zv_loop_create(const struct zv_loop_config_s * config);

ZV_EXPORT enum zv_loop_backend_e zv_loop_get_backend(zv_loop_t * loop);
ZV_EXPORT zv_thread_pool_t * zv_loop_get_thread_pool(zv_loop_t * loop);
ZV_EXPORT zv_timer_mgr_t * zv_loop_get_timer_mgr(zv_loop_t * loop);

ZV_EXPORT void zv_loop_run(zv_loop_t * loop);

ZV_EXPORT void zv_loop_stop(zv_loop_t * loop);

ZV_EXPORT void zv_loop_destroy(zv_loop_t * loop);

enum zv_log_level;
ZV_EXPORT void zv_loop_set_log_level(enum zv_log_level level);

#endif /* !defined(ZV_LOOP_H) */
