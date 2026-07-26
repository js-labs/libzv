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

#include "zv/config.h"
#include <assert.h>


const char * zv_loop_type_get_name(enum zv_loop_type_e loop_type)
{
    switch (loop_type)
    {
        case zv_loop_type_select:
            return "SELECT";
#if defined(_WIN32) || defined(__linux__)
        case zv_loop_type_poll:
            return "POLL";
#endif
        default:
            assert(0);
            return "<UNKNOWN>";
    }
}


const struct zv_loop_config_s * zv_loop_config_get_default()
{
    static const struct zv_loop_config_s config = 
    {
        .loop_type =
#if defined(_WIN32) || defined(__linux__)
            zv_loop_type_poll,
#else
            zv_loop_type_select,
#endif
        .thread_pool =
        {
            .threads = 0 // evaluated depending on number of available cores
        },
        .channel =
        {
            .in_queue =
            {
                .low_watermark = (128 * 1024),
                .high_watermark = (256 * 1024)
            },
            .iov_max = 0 // use IOV_MAX
        },
        .timer_mgr =
        {
            0, // use specific clock
            0  // clockid
        }
    };
    return &config;
}
