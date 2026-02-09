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

#include "zv/os_time.h"

#if defined(_WIN32)

int usleep(unsigned int usec)
{
    Sleep(usec / 1000);
    return 0;
}

int gettimeofday(struct timeval * tv, struct timezone * tz)
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER uli = { .LowPart = ft.dwLowDateTime, .HighPart = ft.dwHighDateTime };
    uli.QuadPart -= 116444736000000000ULL; // adjust 1601 -> 1970
    uli.QuadPart /= 10;
    tv->tv_sec = (long) (uli.QuadPart / 1000000ULL);
    tv->tv_usec = (long) (uli.QuadPart % 1000000ULL);
    return 0;
}

int clock_gettime(clockid_t clockid, struct timespec * ts)
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER uli = { .LowPart = ft.dwLowDateTime, .HighPart = ft.dwHighDateTime };
    uli.QuadPart -= 116444736000000000ULL; // adjust 1601 -> 1970
    ts->tv_sec = (uli.QuadPart / 10000000ULL);
    ts->tv_nsec = (uli.QuadPart % 10000000ULL) * 100;
    return 0;
}

#endif /* _WIN32 */

long zv_elapsed_time_us(const struct timespec * start, const struct timespec * end)
{
    // (end->tv_sec * C + end->tv_nsec) - (start->tv_sec * C + start->tv_nsec)
    long long vv = (end->tv_sec - start->tv_sec);
    vv *= (1000 * 1000);
    vv += (end->tv_nsec - start->tv_nsec) / 1000;
    return (long) vv;
}
