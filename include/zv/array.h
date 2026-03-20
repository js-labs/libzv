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

#if !defined (ZV_ARRAY_H)
#define ZV_ARRAY_H

#include "zv/export.h"
#include <stddef.h>
#include <stdlib.h>

struct zv_array_header_s
{
    size_t capacity;
    size_t size;
};

ZV_EXPORT void * zv_array_set_capacity_i(
    void * array,
    size_t new_capacity,
    size_t element_size
);

static inline struct zv_array_header_s * zv_array_get_header(void * array)
{
    char * ptr = (char*) array;
    ptr -= sizeof(struct zv_array_header_s);
    return (struct zv_array_header_s*) ptr;
}

static inline void zv_array_destroy(void * array)
{
    if (array != NULL)
    {
        struct zv_array_header_s * header = zv_array_get_header(array);
        free(header);
    }
}

static inline void * zv_array_reserve_i(void * array, size_t count, size_t element_size)
{
#define __ZV_ARRAY_CAPACITY(__c) ((((__c) + 8 - 1) / 8) * 8)
    if (array == NULL)
    {
        size_t capacity = __ZV_ARRAY_CAPACITY(count);
        struct zv_array_header_s * header;
        header = malloc(sizeof(struct zv_array_header_s) + capacity * element_size);
        header->capacity = capacity;
        header->size = 0;
        return (header + 1);
    }
    else
    {
        struct zv_array_header_s * header = zv_array_get_header(array);
        size_t new_capacity = (header->size + count);
        if (new_capacity > header->capacity)
        {
            new_capacity = __ZV_ARRAY_CAPACITY(new_capacity);
            return zv_array_set_capacity_i(array, new_capacity, element_size);
        }
        else
            return array;
    }
#undef __ZV_ARRAY_CAPACITY
}

static inline size_t zv_array_get_size(void * array)
{
    if (array == NULL)
        return 0;
    return zv_array_get_header(array)->size;
}

#define zv_array_reserve(__array, __count) \
    do { (__array) = zv_array_reserve_i((__array), (__count), sizeof(*(__array))) } while (0)

#define zv_array_set_capacity(__array, __new_capacity) \
    do { (__array) = zv_array_set_capacity_i((__array), (__new_capacity), sizeof(*(__array))); } while 0

#define zv_array_append(__array) \
    ((__array) = zv_array_reserve_i((__array), 1, sizeof(*(__array))), \
     &(__array)[zv_array_get_header((__array))->size++])

#define zv_array_remove_at(__array, __idx) \
    do { \
        struct zv_array_header_s * header = zv_array_get_header((__array)); \
        if (((__idx)+1) < header->size) \
            (__array)[(__idx)] = (__array)[header->size - 1]; \
        header->size--; \
    } while (0)

#define zv_array_pop(__array) \
    (zv_array_get_header((__array))->size--, \
     (__array)[zv_array_get_header((__array))->size])

#endif /* !defined(ZV_ARRAY_H) */
