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

#define CLOCK_MONOTONIC 1
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

#if !defined(ZV_HAS_RDTSC)

#if defined(_M_X64) || defined(__x86_64__)

#if defined(_MSC_VER)

#include <stdint.h>
#define ZV_HAS_RDTSC 1

static inline uint64_t zv_rdtsc() { return __rdtsc(); }

#elif defined(__GNUC__)

#include <stdint.h>
#define ZV_HAS_RDTSC 1

static inline uint64_t zv_rdtsc()
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
    uint64_t ret = hi;
    ret <<= 32;
    ret |= lo;
    return ret;
}

#else
#error Not supported
#endif
#endif /* defined(_M_X64) || defined(__x86_64__) */
#endif /* ZV_HAS_RDTSC */

#if defined(ZV_HAS_RDTSC) && ZV_HAS_RDTSC
ZV_EXPORT uint64_t zv_tsc_to_usec(uint64_t tsc);
#endif

ZV_EXPORT long zv_elapsed_time_us(const struct timespec * start, const struct timespec * end);

#endif /* !defined(ZV_OS_TIME_H) */
