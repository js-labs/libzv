/*
 * Copyright (C) 2025 Sergey Zubarev, info@js-labs.org
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

#include "zv/buffer.h"
#include <stdio.h>

int buffer_cache()
{
    size_t buffer_size = 100;
    zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(buffer_size, 0, 8);
    struct zv_buffer_s * head = zv_buffer_cache_get_for(buffer_cache, buffer_size*4 - 1);
    size_t count = 0;
    for (struct zv_buffer_s * buffer=head; buffer!=NULL; buffer=buffer->next, count++);
    int rc = 0;
    if (count != 4)
    {
        fprintf(stderr, "got %zu buffers insted of 4\n", count);
        rc = -1;
    }
    zv_buffer_cache_put(buffer_cache, head);
    zv_buffer_cache_release(buffer_cache);
    return rc;
}
