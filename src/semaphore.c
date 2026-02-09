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

#include "zv/semaphore.h"

#if defined(_WIN32)

int sem_init(sem_t * sem, int pshared, int value)
{
    if (pshared)
        return -1;

    InitializeCriticalSection(&sem->cs);
    InitializeConditionVariable(&sem->cv);
    sem->value = value;

    return 0;
}

int sem_destroy(sem_t * sem)
{
    DeleteCriticalSection(&sem->cs);
    return 0;
}

int sem_wait(sem_t * sem)
{
    EnterCriticalSection(&sem->cs);
    while (sem->value == 0)
        SleepConditionVariableCS(&sem->cv, &sem->cs, INFINITE);
    sem->value--;
    LeaveCriticalSection(&sem->cs);
    return 0;
}

int sem_post(sem_t * sem)
{
    EnterCriticalSection(&sem->cs);
    sem->value++;
    WakeConditionVariable(&sem->cv);
    LeaveCriticalSection(&sem->cs);
    return 0;
}

#endif
