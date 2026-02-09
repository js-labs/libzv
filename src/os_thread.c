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

#endif
