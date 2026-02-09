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

#if !defined(ZV_SEMAPHORE_H)
#define ZV_SEMAPHORE_H

#if defined(__linux__)

#include <semaphore.h>

#elif defined(__APPLE__)

#include <dispatch/dispatch.h>

typedef dispatch_semaphore_t sem_t;

static inline int sem_init(sem_t * sem, int pshared, int value) { *sem = dispatch_semaphore_create(value); return (*sem == NULL); }
static inline int sem_destroy(sem_t * sem) { dispatch_release(*sem); return 0; }
static inline long sem_wait(sem_t * sem) { return dispatch_semaphore_wait(*sem, DISPATCH_TIME_FOREVER); }
static inline long sem_post(sem_t * sem) { return dispatch_semaphore_signal(*sem); }

#elif defined(_WIN32)

#include <windows.h>
#include "zv/export.h"

typedef struct
{
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    int value;
}
sem_t;

ZV_EXPORT int sem_init(sem_t * sem, int pshared, int value);
ZV_EXPORT int sem_destroy(sem_t * sem);
ZV_EXPORT int sem_wait(sem_t * sem);
ZV_EXPORT int sem_post(sem_t * sem);

#else
#error Not supported
#endif

#endif /* !defined(ZV_SEMAPHORE_H) */
