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

#if !defined(ZV_OS_THREAD_H)
#define ZV_OS_THREAD_H

#if defined(_WIN32)

#include <windows.h>
#include <stdlib.h>
#include "zv/export.h"
#include "zv/os_time.h"

typedef DWORD pid_t;
typedef DWORD tid_t;
typedef HANDLE pthread_t;
typedef struct pthread_attr_s pthread_attr_t;
typedef CRITICAL_SECTION pthread_mutex_t;
typedef struct pthread_mutexattr_s pthread_mutexattr_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef struct pthread_condattr_s { char _dummy; } pthread_condattr_t;
typedef DWORD pthread_key_t;

ZV_EXPORT int pthread_create(
    pthread_t * thread,
    const pthread_attr_t * attr,
    void *(*start_routine)(void*),
    void * arg
);

static inline int pthread_join(pthread_t thread, void ** retval)
{
    WaitForSingleObject(thread, INFINITE);
    return 0;
}

static inline int pthread_mutex_init(
    pthread_mutex_t * mutex,
    const pthread_mutexattr_t * attr
) {
    InitializeCriticalSection(mutex);
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t * mutex)
{
    DeleteCriticalSection(mutex);
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t * mutex)
{
    EnterCriticalSection(mutex);
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t * mutex)
{
    LeaveCriticalSection(mutex);
    return 0;
}

static inline int pthread_cond_init(
    pthread_cond_t * cond,
    const pthread_condattr_t * attr
) {
    InitializeConditionVariable(cond);
    return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t * cond)
{
    return 0;
}

static inline int pthread_cond_wait(
    pthread_cond_t * cond,
    pthread_mutex_t * mutex
) {
    SleepConditionVariableCS(cond, mutex, INFINITE);
    return 0;
}

static inline int pthread_cond_timedwait(
    pthread_cond_t * cond,
    pthread_mutex_t * mutex,
    const struct timespec * abstime
) {
    for (;;)
    {
        struct timeval now;
        gettimeofday(&now, NULL);
        if (now.tv_sec > abstime->tv_sec)
            return ETIMEDOUT;

        const long now_ns = (now.tv_usec * 1000);
        if ((now.tv_sec == abstime->tv_sec) && (now_ns >= abstime->tv_nsec))
            return ETIMEDOUT;

        const time_t sleep_sec = (abstime->tv_sec - now.tv_sec);
        const time_t max_sleep_sec = ((INFINITE / 1000) - 1);
        DWORD sleep_time_ms;
        if (sleep_sec > max_sleep_sec)
            sleep_time_ms = (DWORD) (max_sleep_sec * 1000);
        else
        {
            sleep_time_ms = (DWORD) (sleep_sec * 1000);
            sleep_time_ms += (abstime->tv_nsec / 1000000);
            sleep_time_ms -= (now_ns / 1000000);
        }

        BOOL rc = SleepConditionVariableCS(cond, mutex, sleep_time_ms);
        if (rc)
            return 0;

        DWORD last_error = GetLastError();
        if (last_error != ERROR_TIMEOUT)
            return EINVAL;

        // calculate sleep time once more on the next loop iteration
        // for a case if it was larger than DWORD
    }
    return 0;
}

static inline int pthread_cond_signal(pthread_cond_t * cond)
{
    WakeConditionVariable(cond);
    return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t * cond)
{
    WakeAllConditionVariable(cond);
    return 0;
}

static inline int pthread_condattr_init(pthread_condattr_t * condattr)
{
    return 0;
}

static inline int pthread_condattr_destroy(pthread_condattr_t * condattr)
{
    return 0;
}

static inline int pthread_key_create(pthread_key_t * key, void (*destructor)(void*))
{
    pthread_key_t k = FlsAlloc(destructor);
    if (k == FLS_OUT_OF_INDEXES)
        return GetLastError();
    else
    {
        *key = k;
        return 0;
    }
}

static inline int pthread_key_delete(pthread_key_t key)
{
    return FlsFree(key) != 0;
}

static inline int pthread_setspecific(pthread_key_t key, const void * value)
{
    return FlsSetValue(key, (void*)value) != 0;
}

static inline void * pthread_getspecific(pthread_key_t key)
{
    return FlsGetValue(key);
}

static inline pid_t gettid()
{
    return GetCurrentThreadId();
}

#else

#include <pthread.h>

#if defined(__APPLE__)

#include <stdint.h>

typedef uint64_t tid_t;

static inline tid_t gettid()
{
    tid_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid;
}

#else

typedef pid_t tid_t;

#endif /* __APPLE__ */

#endif /* _WIN32 */

#endif /* !defined(ZV_OS_THREAD_H) */
