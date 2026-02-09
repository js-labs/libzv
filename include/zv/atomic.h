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

#if !defined(ZV_ATOMIC_H)
#define ZV_ATOMIC_H

#if defined(_MSC_VER)
#include <intrin.h>
#endif

static inline void zv_atomic_wait()
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
#endif
}

#if defined(__GNUC__)

#define zv_atomic_add_fetch(ptr, val, memorder) __atomic_add_fetch((ptr), (val), (memorder))
#define zv_atomic_cas(ptr, poldval, newval) __atomic_compare_exchange_n((ptr), (poldval), (newval), 0, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)
#define zv_atomic_load(ptr) __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
#define zv_atomic_store(ptr, val) __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
#define zv_atomic_sub_fetch(ptr, val) __atomic_sub_fetch((ptr), (val), __ATOMIC_RELEASE)
#define zv_atomic_swap(ptr, val) __atomic_exchange_n((ptr), (val), __ATOMIC_RELEASE)

#elif defined(_MSC_VER)

static inline long _zv_atomic_add_fetch_l(long volatile * ptr, long value)
{
    long oldval = _InterlockedExchangeAdd(ptr, value);
    return (oldval + value);
}

static inline long long _zv_atomic_add_fetch_ll(long long volatile * ptr, long long value)
{
    long long oldval = _InterlockedExchangeAdd64(ptr, value);
    return (oldval + value);
}

static inline unsigned long long _zv_atomic_add_fetch_ull(
    unsigned long long volatile * ptr,
    unsigned long long value
) {
    unsigned long long oldval = _InterlockedExchangeAdd64(ptr, value);
    return (oldval + value);
}

// memorder not used with MSC
#define zv_atomic_add_fetch(ptr, val, memorder) \
    _Generic((ptr), \
        long*: _zv_atomic_add_fetch_l, \
        long long*: _zv_atomic_add_fetch_ll, \
        unsigned long long*: _zv_atomic_add_fetch_ull)((ptr), (val))

static inline int _zv_atomic_cas_ull(
    unsigned long long volatile * ptr,
    unsigned long long * poldval,
    unsigned long long newval
) {
    unsigned long long oldval = *poldval;
    *poldval = _InterlockedCompareExchange64(ptr, newval, oldval);
    return (*poldval == oldval);
}

static inline int _zv_atomic_cas_ptr(void * volatile * ptr, void ** poldval, void * newval)
{
    void * oldval = *poldval;
    *poldval = _InterlockedCompareExchangePointer(ptr, newval, oldval);
    return (*poldval == oldval);
}

#define zv_atomic_cas(ptr, poldval, newval) \
    _Generic((ptr), \
        unsigned long long*: _zv_atomic_cas_ull, \
        default: _zv_atomic_cas_ptr)((ptr), (poldval), (newval))

// but would be nice to cast a pointer to volatile

static inline unsigned long long _zv_atomic_load_ull(unsigned long long volatile * ptr) { return *ptr; }
static inline void * _zv_atomic_load_ptr(void * volatile * ptr) { return * ptr; }

#define zv_atomic_load(ptr) \
    _Generic((ptr), \
        unsigned long long*: _zv_atomic_load_ull, \
        default: _zv_atomic_load_ptr)((ptr))

static inline void _zv_atomic_store_ull(unsigned long long volatile * ptr, unsigned long long val) { *ptr = val; }
static inline void _zv_atomic_store_ptr(void * volatile * ptr, void * val) { *ptr = val; }

#define zv_atomic_store(ptr, val) \
    _Generic((ptr), \
        unsigned long long*: _zv_atomic_store_ull, \
        default: _zv_atomic_store_ptr)((ptr), (val))

static inline long _zv_atomic_sub_fetch_l(long volatile * ptr, long value)
{
    long oldval = _InterlockedExchangeAdd(ptr, 0 - value);
    return (oldval - value);
}

static inline long long _zv_atomic_sub_fetch_ll(long long volatile * ptr, long long value)
{
    long long oldval = _InterlockedExchangeAdd64(ptr, 0 - value);
    return (oldval - value);
}

static inline unsigned long long _zv_atomic_sub_fetch_ull(
    unsigned long long volatile * ptr,
    unsigned long long value
) {
    unsigned long long oldval = _InterlockedExchangeAdd64(ptr, 0 - value);
    return (oldval - value);
}

#define zv_atomic_sub_fetch(ptr, val) \
    _Generic((ptr), \
        long*: _zv_atomic_sub_fetch_l, \
        long long*: _zv_atomic_sub_fetch_ll, \
        unsigned long long*: _zv_atomic_sub_fetch_ull)((ptr), (val))

#define zv_atomic_swap(ptr, val) \
    _Generic((ptr), \
        unsigned long long*: _InterlockedExchange64, \
        default: _InterlockedExchangePointer)((ptr), (val))

#else
#error Platform/compiler not supported
#endif

#endif /* !defined(ZV_ATOMIC_H) */
