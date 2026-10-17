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
#include <errno.h>
#include "zv/export.h"
#include "zv/os_time.h"

typedef DWORD pid_t;
typedef DWORD tid_t;
typedef struct zv_thread_s * pthread_t;
typedef INIT_ONCE pthread_once_t;
typedef struct pthread_attr_s pthread_attr_t;
typedef SRWLOCK pthread_mutex_t;
typedef struct pthread_mutexattr_s pthread_mutexattr_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef struct pthread_condattr_s { char _dummy; } pthread_condattr_t;
typedef DWORD pthread_key_t;

#define PTHREAD_ONCE_INIT INIT_ONCE_STATIC_INIT
#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
#define PTHREAD_COND_INITIALIZER CONDITION_VARIABLE_INIT

#if defined(__cplusplus)
extern "C" {
#endif

ZV_EXPORT pthread_t pthread_self();

ZV_EXPORT int pthread_equal(pthread_t t1, pthread_t t2);

ZV_EXPORT int pthread_create(
    pthread_t * ret,
    const pthread_attr_t * attr,
    void *(*start_routine)(void*),
    void * arg
);

ZV_EXPORT int pthread_join(pthread_t thread, void ** retval);

ZV_EXPORT int pthread_detach(pthread_t thread);

ZV_EXPORT int pthread_once(
    pthread_once_t * once_control,
    void (*init_routine)(void)
);

static inline int pthread_mutex_init(
    pthread_mutex_t * mutex,
    const pthread_mutexattr_t * attr
) {
    InitializeSRWLock(mutex);
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t * mutex)
{
    // SRW lock does not need to be destroyed
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t * mutex)
{
    AcquireSRWLockExclusive(mutex);
    return 0;
}

static inline int pthread_mutex_trylock(pthread_mutex_t * mutex)
{
    return TryAcquireSRWLockExclusive(mutex) ? 0 : EBUSY;
}

static inline int pthread_mutex_unlock(pthread_mutex_t * mutex)
{
    ReleaseSRWLockExclusive(mutex);
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
    SleepConditionVariableSRW(cond, mutex, INFINITE, 0);
    return 0;
}

ZV_EXPORT int pthread_cond_timedwait(
    pthread_cond_t * cond,
    pthread_mutex_t * mutex,
    const struct timespec * abstime
);

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

#if defined(__cplusplus)
}
#endif

#else

#include <pthread.h>

#if defined(__APPLE__)

#include <stdint.h>

typedef uint64_t tid_t;

#if defined(__cplusplus)
extern "C" {
#endif

static inline tid_t gettid()
{
    tid_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid;
}

#if defined(__cplusplus)
}
#endif

#else

typedef pid_t tid_t;

#endif /* __APPLE__ */

#endif /* _WIN32 */

#endif /* !defined(ZV_OS_THREAD_H) */
