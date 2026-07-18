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

#include <stddef.h>

#if defined(_WIN32)

#include <WinSock2.h>

static inline int _zv_get_errno() { return WSAGetLastError(); }
static inline void _zv_set_errno(int n) { WSASetLastError(n); }

#elif defined(__linux__) || defined(__APPLE__)

#include <sys/errno.h>

static inline int _zv_get_errno() { return errno; }
static inline void _zv_set_errno(int n) { errno = n; }

#endif

#define _ZV_CONTAINER_OF(ptr, type, member) \
  ((type*) ((char*) (ptr) - offsetof(type, member)))

static inline size_t _zv_round(size_t v, size_t m)
{
    return ((v + m - 1) / m) * m;
}

static inline void _zv_atomic_wait()
{
#if defined(_M_IX86)
    __asm{_emit 0xf3}; __asm{_emit 0x90}; /* MSC */
#elif defined(_M_X64)
    _mm_pause(); /* MSC */
#elif defined(__i386__) || defined(__x86_64__)
    #if defined(__GNUC__)
        __asm__ __volatile__("rep; nop\n");
    #else
        #warning Compiler not supported
    #endif
#elif defined(__ARM_ARCH)
    #if defined(__GNUC__)
        __asm__ __volatile__("yield");
    #else
        #warning Compiler not supported
    #endif
#else
#error Platform/compiler not supported
#endif
}

#endif /* !defined(ZV_MISC_H) */
