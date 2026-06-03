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

#if !defined(ZV_BUFFER_H)
#define ZV_BUFFER_H

#include <stddef.h>
#include "zv/export.h"

struct zv_buffer_s
{
    struct zv_buffer_s * next;
};

typedef enum zv_log_level_e zv_log_level_t;
typedef struct zv_buffer_cache_s zv_buffer_cache_t;

ZV_EXPORT zv_buffer_cache_t * zv_buffer_cache_create(
    size_t buffer_size,
    size_t max_buffers,
    size_t init_buffers
);

ZV_EXPORT void zv_buffer_cache_retain(zv_buffer_cache_t * buffer_cache);
ZV_EXPORT void zv_buffer_cache_release(zv_buffer_cache_t * buffer_cache);

ZV_EXPORT size_t zv_buffer_cache_get_buffer_size(zv_buffer_cache_t * buffer_cache);
ZV_EXPORT struct zv_buffer_s * zv_buffer_cache_get(zv_buffer_cache_t * buffer_cache);
ZV_EXPORT struct zv_buffer_s * zv_buffer_cache_get_for(zv_buffer_cache_t * buffer_cache, size_t size);
ZV_EXPORT void zv_buffer_cache_put(zv_buffer_cache_t * buffer_cache, struct zv_buffer_s * buffer);

ZV_EXPORT void zv_buffer_cache_set_log_level(zv_log_level_t level);

#endif /* !defined(ZV_BUFFER_H) */
