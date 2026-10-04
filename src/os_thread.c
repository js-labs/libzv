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

struct zv_thread_arg_s
{
    void* (*start_routine)(void*);
    void* arg;
};


static inline DWORD _zv_thread_func(void * arg)
{
    struct zv_thread_arg_s * thread_arg = arg;
    void * (*start_routine)(void*) = thread_arg->start_routine;
    arg = thread_arg->arg;
    free(thread_arg);
    start_routine(arg);
    return 0;
}


int pthread_create(
    pthread_t * thread,
    const pthread_attr_t * attr,
    void * (*start_routine)(void*),
    void * arg
) {
    struct zv_thread_arg_s * thread_arg = malloc(sizeof(struct zv_thread_arg_s));
    if (thread_arg)
    {
        thread_arg->start_routine = start_routine;
        thread_arg->arg = arg;
        HANDLE hThread = CreateThread(NULL, 0, &_zv_thread_func, thread_arg, 0, NULL);
        if (hThread)
        {
            *thread = hThread;
            return 0;
        }
        free(thread_arg);
    }
    return -1;
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
