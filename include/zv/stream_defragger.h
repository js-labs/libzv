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

#if !defined(ZV_STREAM_DEFRAGGER_H)
#define ZV_STREAM_DEFRAGGER_H

#include "zv/export.h"
#include <stddef.h>

struct zv_stream_defragger_s
{
    size_t buffer_capacity;
    size_t buffer_size;
    void * buffer;
};

struct zv_stream_defragger_state_s
{
    const void * data;
    size_t size;
};

ZV_EXPORT void zv_stream_defragger_init(struct zv_stream_defragger_s * stream_defragger);
ZV_EXPORT void zv_stream_defragger_deinit(struct zv_stream_defragger_s * stream_defragger);

ZV_EXPORT const void * zv_stream_defragger_get_next(
    struct zv_stream_defragger_s * stream_defragger,
    size_t header_size,
    size_t (*message_size_func)(const void *),
    struct zv_stream_defragger_state_s * state,
    size_t * message_size
);

#endif /* !defined(ZV_STREAM_DEFRAGGER_H) */
