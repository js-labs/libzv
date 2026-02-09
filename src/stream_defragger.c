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

#include "zv/stream_defragger.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


void zv_stream_defragger_init(struct zv_stream_defragger_s * stream_defragger)
{
    stream_defragger->buffer_capacity = 0;
    stream_defragger->buffer_size = 0;
    stream_defragger->buffer = NULL;
}


void zv_stream_defragger_deinit(struct zv_stream_defragger_s * stream_defragger)
{
    free(stream_defragger->buffer);
}


static void _zv_stream_defragger_append(
    struct zv_stream_defragger_s * stream_defragger,
    const void * data,
    size_t size
) {
    size_t new_size = (stream_defragger->buffer_size + size);
    size_t capacity = stream_defragger->buffer_capacity;
    if (new_size > capacity)
    {
        if (capacity == 0)
            capacity = 1;
        while (capacity < new_size) { capacity *= 2; }
        void * buffer = malloc(capacity);
        memcpy(buffer, stream_defragger->buffer, stream_defragger->buffer_size);
        free(stream_defragger->buffer);
        stream_defragger->buffer_capacity = capacity;
        stream_defragger->buffer = buffer;
    }
    void * dst = ((char*)stream_defragger->buffer) + stream_defragger->buffer_size;
    memcpy(dst, data, size);
    stream_defragger->buffer_size = new_size;
}


const void * zv_stream_defragger_get_next(
    struct zv_stream_defragger_s * stream_defragger,
    size_t header_size,
    size_t (*message_size_func)(const void * data),
    struct zv_stream_defragger_state_s * state,
    size_t * pmessage_size
) {
    const void * data = state->data;
    size_t size = state->size;
    if (stream_defragger->buffer_size != 0)
    {
        size_t cc = (stream_defragger->buffer_size + size);
        if (cc < header_size)
        {
            _zv_stream_defragger_append(stream_defragger, data, size);
            *pmessage_size = 0;
            return NULL;
        }

        if (stream_defragger->buffer_size < header_size)
        {
            cc = (header_size - stream_defragger->buffer_size);
            _zv_stream_defragger_append(stream_defragger, data, cc);
            data = (((const char*) data) + cc);
            size -= cc;
        }

        const size_t message_size = message_size_func(stream_defragger->buffer);
        if (message_size == SIZE_MAX)
        {
            *pmessage_size = SIZE_MAX;
            return NULL;
        }

        cc = (message_size - stream_defragger->buffer_size);
        if (cc > 0)
        {
            if (size < cc)
            {
                if (size > 0)
                    _zv_stream_defragger_append(stream_defragger, data, size);
                *pmessage_size = 0;
                return NULL;
            }

            _zv_stream_defragger_append(stream_defragger, data, cc);
            data = (((const char*) data) + cc);
            size -= cc;
        }

        stream_defragger->buffer_size = 0;
        state->data = data;
        state->size = size;
        *pmessage_size = message_size;
        return stream_defragger->buffer;
    }

    if (size >= header_size)
    {
        const size_t message_size = message_size_func(state->data);
        if (message_size == SIZE_MAX)
        {
            *pmessage_size = SIZE_MAX;
            return NULL;
        }

        if (size >= message_size)
        {
            state->data = (((const char *)data) + message_size);
            state->size = (size - message_size);
            *pmessage_size = message_size;
            return data;
        }
    }

    if (size > 0)
        _zv_stream_defragger_append(stream_defragger, data, size);

    *pmessage_size = 0;
    return NULL;
}
