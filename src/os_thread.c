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

#include "zv/os_thread.h"

#if defined(_WIN32)

static DWORD _zv_thread_tls_index = 0;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    void * ptr;
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            _zv_thread_tls_index = TlsAlloc();
            if (_zv_thread_tls_index == TLS_OUT_OF_INDEXES)
                return FALSE;
            break;

        case DLL_THREAD_ATTACH:
            break;

        case DLL_THREAD_DETACH:
            ptr = TlsGetValue(_zv_thread_tls_index);
            if (ptr != NULL)
            {
                uintptr_t uptr = (uintptr_t) ptr;
                if (uptr & 1)
                {
                    ptr = (void*) (uptr ^ 1);
                    LocalFree(ptr);
                }
            }
            break;

        case DLL_PROCESS_DETACH:
            TlsFree(_zv_thread_tls_index);
            break;

        default:
            break;
    }

    return TRUE;
}


struct zv_thread_s
{
    volatile LONG ref_count;
    HANDLE handle;
    DWORD id;
    void * (*start_routine)(void*);
    void * arg;
    void * retval;
};


pthread_t pthread_self()
{
    void * ptr = TlsGetValue(_zv_thread_tls_index);
    if ((GetLastError() == ERROR_SUCCESS) && (ptr == NULL))
    {
        ptr = LocalAlloc(LMEM_FIXED, sizeof(DWORD));
        if (ptr != NULL)
        {
            *((DWORD*)ptr) = GetCurrentThreadId();
            ptr = (void*) (((uintptr_t)ptr) | 1);
            TlsSetValue(_zv_thread_tls_index, ptr);
        }
    }
    return ptr;
}


int pthread_equal(pthread_t t1, pthread_t t2)
{
    uintptr_t lt1 = (uintptr_t) t1;
    uintptr_t lt2 = (uintptr_t) t2;
    if (lt1 & 1)
    {
        if (lt2 & 1)
            return (t1 == t2);
        else
        {
            DWORD t1_id = *((DWORD*) (void*) (lt1 ^ 1));
            return (t1_id == t2->id);
        }
    }
    else
    {
        if (lt2 & 1)
        {
            DWORD t2_id = *((DWORD*) (void*) (lt2 ^ 1));
            return (t1->id == t2_id);
        }
        else
            return (t1 == t2);
    }
}


static DWORD WINAPI _zv_thread_func(void * arg)
{
    TlsSetValue(_zv_thread_tls_index, arg);
    struct zv_thread_s * thread = arg;
    thread->retval = thread->start_routine(thread->arg);
    TlsSetValue(_zv_thread_tls_index, NULL);
    if (InterlockedDecrement(&thread->ref_count) == 0)
        free(thread);
    return 0;
}


int pthread_create(
    pthread_t * ret,
    const pthread_attr_t * attr,
    void * (*start_routine)(void*),
    void * arg
) {
    struct zv_thread_s * thread = malloc(sizeof(struct zv_thread_s));
    if (thread == NULL)
        return ENOMEM;

    thread->ref_count = 2; // thread + (join or detach)
    thread->start_routine = start_routine;
    thread->arg = arg;
    thread->retval = NULL;
    HANDLE hThread = CreateThread(NULL, 0, &_zv_thread_func, thread, 0, &thread->id);
    if (hThread != NULL)
    {
        thread->handle = hThread;
        *ret = thread;
        return 0;
    }
    free(thread);
    return EAGAIN;
}


int pthread_join(pthread_t thread, void ** retval)
{
    if (((uintptr_t)thread) & 1)
    {
        // result of pthread_self() not supposed to be joined
        return EINVAL;
    }

    if (WaitForSingleObject(thread->handle, INFINITE) != WAIT_OBJECT_0)
    {
        // not sure it make sense to continue in case of error here
        return EINVAL;
    }

    if (retval != NULL)
        *retval = thread->retval;

    CloseHandle(thread->handle);

    if (InterlockedDecrement(&thread->ref_count) == 0)
        free(thread);

    return 0;
}


int pthread_detach(pthread_t thread)
{
    if (((uintptr_t)thread) & 1)
    {
        // result of pthread_self() not supposed to be detached
        return EINVAL;
    }

    CloseHandle(thread->handle);

    if (InterlockedDecrement(&thread->ref_count) == 0)
        free(thread);

    return 0;
}


static BOOL CALLBACK _zv_pthread_once_shim(PINIT_ONCE once, PVOID param, PVOID * ctx)
{
    ((void(*)(void))param)();
    return TRUE;
}


int pthread_once(
    pthread_once_t * once_control,
    void (*init_routine)(void)
) {
    BOOL status = InitOnceExecuteOnce(
        once_control,
        &_zv_pthread_once_shim,
        (PVOID) init_routine,
        NULL
    );
    return status ? 0 : -1;
}


int pthread_cond_timedwait(
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
            sleep_time_ms = (DWORD)(max_sleep_sec * 1000);
        else
        {
            sleep_time_ms = (DWORD)(sleep_sec * 1000);
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

#endif
