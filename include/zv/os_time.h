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

#if !defined(ZV_OS_TIME_H)
#define ZV_OS_TIME_H

#include "zv/export.h"

#if defined(_WIN32)
#include <Winsock2.h>
#include <stdint.h>
#include <time.h>

ZV_EXPORT int usleep(unsigned int usec);
ZV_EXPORT int gettimeofday(struct timeval * tv, struct timezone * tz);

#define CLOCK_REALTIME  1
#define CLOCK_MONOTONIC 2
typedef int clockid_t;

ZV_EXPORT int clock_gettime(clockid_t clockid, struct timespec *ts);

#elif defined(__linux__)
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/time.h>
#include <unistd.h>
#else
#error Not supported
#endif

ZV_EXPORT long zv_elapsed_time_us(
    const struct timespec * start,
    const struct timespec * end
);

ZV_EXPORT void zv_timespec_add_ms(struct timespec * ts, long ms);

#endif /* !defined(ZV_OS_TIME_H) */
