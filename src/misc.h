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

#if !defined(ZV_MISC_H)
#define ZV_MISC_H

#if defined(_WIN32)

static inline int _zv_get_errno() { return WSAGetLastError(); }
static inline void _zv_set_errno(int n) { WSASetLastError(n); }

#elif defined(__linux__) || defined(__APPLE__)

#include <sys/errno.h>
#include <unistd.h>

static inline int _zv_get_errno() { return errno; }
static inline void _zv_set_errno(int n) { errno = n; }

#endif

static inline size_t _zv_round(size_t v, size_t m)
{
    return ((v + m - 1) / m) * m;
}

#endif /* !defined(ZV_MISC_H) */
