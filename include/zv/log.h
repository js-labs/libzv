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

#if !defined(ZV_LOG_H)
#define ZV_LOG_H

#include <stdlib.h>
#include <stdio.h>
#include "zv/export.h"

#if defined(__cplusplus)
extern "C" {
#endif

enum zv_log_level
{
    zv_log_level_error = 1,
    zv_log_level_warning,
    zv_log_level_info,
    zv_log_level_debug,
    zv_log_level_trace
};

ZV_EXPORT const char * zv_log_level_get_name(enum zv_log_level log_level);

struct zv_log_layer_s
{
    const char * name;
    enum zv_log_level level;
};

typedef struct zv_log_event_s zv_log_event_t;

ZV_EXPORT zv_log_event_t * zv_log_event_create(
    const struct zv_log_layer_s * layer,
    enum zv_log_level level,
    const char * file,
    int line,
    const char * fmt,
    ...
);

typedef struct zv_log_backend_s zv_log_backend_t;

ZV_EXPORT zv_log_backend_t * zv_log_add_backend(
    void (*func)(zv_log_event_t *, void*),
    void (*flush)(void*),
    void * thunk
);

ZV_EXPORT zv_log_backend_t * zv_log_add_file_backend(FILE * file);
ZV_EXPORT int zv_log_remove_backend(zv_log_backend_t * backend, void ** pthunk);
ZV_EXPORT FILE * zv_log_remove_file_backend(zv_log_backend_t * backend);

ZV_EXPORT void zv_log(zv_log_event_t * log_event);
ZV_EXPORT void zv_log_flush();

static inline int zv_log_level_enabled(
    const struct zv_log_layer_s * layer,
    enum zv_log_level level
) {
    return (layer->level >= level);
}

#define ZV_LOG(layer, level, ...) \
    do { \
        if (zv_log_level_enabled(&layer, level)) { \
            zv_log_event_t * log_event = zv_log_event_create( \
                &layer, level, __FILE__, __LINE__, __VA_ARGS__); \
            zv_log(log_event); \
        } \
    } \
    while (0)

#define ZV_LOG_ERROR(layer, ...) ZV_LOG(layer, zv_log_level_error, __VA_ARGS__)
#define ZV_LOG_WARNING(layer, ...) ZV_LOG(layer, zv_log_level_warning, __VA_ARGS__)
#define ZV_LOG_INFO(layer, ...) ZV_LOG(layer, zv_log_level_info, __VA_ARGS__)
#define ZV_LOG_DEBUG(layer, ...) ZV_LOG(layer, zv_log_level_debug, __VA_ARGS__)
#define ZV_LOG_TRACE(layer, ...) ZV_LOG(layer, zv_log_level_trace, __VA_ARGS__)

#if defined(__cplusplus)
}
#endif

#endif /* !defined(ZV_LOG_H) */
