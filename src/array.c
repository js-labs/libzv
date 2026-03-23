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

#include "zv/array.h"
#include <string.h>

void * zv_array_set_capacity_i(void * array, size_t new_capacity, size_t element_size)
{
    if (array == NULL)
    {
        void * ptr = malloc(sizeof(struct zv_array_header_s) + new_capacity * element_size);
        if (ptr == NULL)
            return NULL;
        ((struct zv_array_header_s*)ptr)->capacity = new_capacity;
        ((struct zv_array_header_s*)ptr)->size = 0;
        void * ret = ((char*)ptr) + sizeof(struct zv_array_header_s);
        return ret;
    }
    else
    {
        struct zv_array_header_s * header = zv_array_get_header(array);
        if (new_capacity > header->capacity)
        {
            void * ptr = malloc(sizeof(struct zv_array_header_s) + new_capacity * element_size);
            if (ptr == NULL)
                return NULL;
            ((struct zv_array_header_s*)ptr)->capacity = new_capacity;
            ((struct zv_array_header_s*)ptr)->size = header->size;
            void * ret = ((char*)ptr) + sizeof(struct zv_array_header_s);
            memcpy(ret, array, header->size * element_size);
            free(header);
            return ret;
        }
        else
            return array;
    }
}
