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
#include "zv/semaphore.h"
#include "zv/thread_pool.h"
#include "misc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#if !defined(_WIN32)
#include <signal.h>
#endif

#if defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

#define IDLE_THREAD 0x00000001L
#define IDLE_THREADS_MASK 0x0000FFFFL
#define FL_STOP 0x00010000L

#define CONTENTION_FACTOR 4


struct zv_thread_pool_partition_s
{
    struct zv_thread_pool_task_s * _Atomic head;
    char _padding1[64 - sizeof(void*)];
    struct zv_thread_pool_task_s * _Atomic tail;
    char _padding2[64 - sizeof(void*)];
};


struct zv_thread_pool_s
{
    size_t worker_threads;
    _Atomic size_t state;
    sem_t sem;
    struct zv_thread_pool_partition_s partitions[CONTENTION_FACTOR];
};


static void * _zv_thread_pool_worker(void * arg)
{
    void * HEAD_LOCK = (void*)1;
    pid_t thread_id = gettid();

    zv_thread_pool_t * thread_pool = arg;
    size_t sem_waits = 0;
    size_t head_waits = 0;
    size_t next_waits = 0;

    for (;;)
    {
#if CONTENTION_FACTOR == 1
        for (;;)
        {
            struct zv_thread_pool_partition_s * partition = &thread_pool->partitions[0];
            struct zv_thread_pool_task_s * head = atomic_exchange(&partition->head, HEAD_LOCK);
            if (head == NULL)
            {
                head = HEAD_LOCK;
                if (atomic_compare_exchange_strong(&partition->head, &head, NULL))
                {
                    size_t state = atomic_fetch_add(&thread_pool->state, IDLE_THREAD);
                    if (state & FL_STOP)
                        goto end;
                    else
                        break;
                }
                // zv_thread_pool_dispatch() could modify it from another thread
                continue;
            }

            if (head == HEAD_LOCK)
            {
                head_waits++;
                _zv_atomic_wait();
                continue;
            }

            struct zv_thread_pool_task_s * next = atomic_load(&head->next);
            atomic_store(&partition->head, next);

            if (next == NULL)
            {
                struct zv_thread_pool_task_s * tail = head;
                if (!atomic_compare_exchange_strong(&partition->tail, &tail, NULL))
                {
                    while ((next = atomic_load(&head->next)) == NULL)
                    {
                        next_waits++;
                        _zv_atomic_wait();
                    }
                    atomic_store(&partition->head, next);
                }
            }

            head->next = NULL;
            head->func(head);
        }
#else
        size_t idx = 0;
        size_t cc = 0;
        for (;;)
        {
            struct zv_thread_pool_partition_s * partition = &thread_pool->partitions[idx];

            struct zv_thread_pool_task_s * head = atomic_exchange(&partition->head, HEAD_LOCK);
            if (head == NULL)
            {
                head = HEAD_LOCK;
                if (atomic_compare_exchange_strong(&partition->head, &head, NULL))
                {
                    if (++cc == CONTENTION_FACTOR)
                    {
                        size_t state = atomic_fetch_add(&thread_pool->state, IDLE_THREAD);
                        if (state & FL_STOP)
                            goto end;
                        else
                            break;
                    }
                    idx = ((idx + 1) % CONTENTION_FACTOR);
                }
                continue;
            }

            if (head == HEAD_LOCK)
            {
                head_waits++;
                idx = ((idx + 1) % CONTENTION_FACTOR);
                continue;
            }

            struct zv_thread_pool_task_s * next = atomic_load(&head->next);
            atomic_store(&partition->head, next);

            if (next == NULL)
            {
                struct zv_thread_pool_task_s * tail = head;
                if (!atomic_compare_exchange_strong(&partition->tail, &tail, NULL))
                {
                    while ((next = atomic_load(&head->next)) == NULL)
                    {
                        next_waits++;
                        _zv_atomic_wait();
                    }
                    atomic_store(&partition->head, next);
                }
            }

            head->next = NULL;
            head->func(head);

            idx = ((idx + 1) % CONTENTION_FACTOR);
            cc = 0;
        }
#endif
        sem_waits++;
        sem_wait(&thread_pool->sem);
    }

    end:;
    /*
    FILE * stats = thread_pool->stats;
    if (stats != NULL)
    {
        fprintf(stats, "thread_pool_worker %d: sem_waits=%zd head_waits=%zd next_waits=%zd\n",
            thread_id, sem_waits, head_waits, next_waits);
    }
    */

    return NULL;
}


zv_thread_pool_t * zv_thread_pool_create(size_t worker_threads)
{
    if (worker_threads == 0)
    {
        int cores;
#if defined(_WIN32)
        SYSTEM_INFO system_info;
        GetSystemInfo(&system_info);
        cores = system_info.dwNumberOfProcessors;
#elif defined(__linux__)
        cores = sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(__APPLE__)
        int mib[2];
        size_t len = sizeof(cores);

        mib[0] = CTL_HW;
        mib[1] = HW_AVAILCPU;

        sysctl(mib, 2, &cores, &len, NULL, 0);

        if (cores < 1)
        {
            mib[1] = HW_NCPU;
            sysctl(mib, 2, &cores, &len, NULL, 0);
            if (cores < 1)
                cores = 1;
        }
#else
#error Not implemented
#endif
        worker_threads = (cores / 2);
        if (worker_threads < 2)
            worker_threads = 2;
    }

    if (worker_threads > IDLE_THREADS_MASK)
        worker_threads = IDLE_THREADS_MASK;

    size_t size = _zv_round(sizeof(struct zv_thread_pool_s), sizeof(void*));
    zv_thread_pool_t * thread_pool = malloc(size + sizeof(pthread_t) * worker_threads);
    if (!thread_pool)
        return NULL;

    thread_pool->state = 0;
    int rc = sem_init(&thread_pool->sem, 0, 0);
    if (rc != 0)
    {
        free(thread_pool);
        return NULL;
    }

    for (int idx=0; idx<CONTENTION_FACTOR; idx++)
    {
        struct zv_thread_pool_partition_s * partition = &thread_pool->partitions[idx];
        partition->head = NULL;
        partition->tail = NULL;
    }

#if !defined(_WIN32)
    // Worker threads should not handle signals sent to the process.
    // New threads inherit the signal mask of the creating thread,
    // so block signals for the time of worker threads creation,
    // this way there is no window when a signal could be delivered to a worker thread.
    // Synchronous signals generated by a fault in a task are left unblocked,
    // blocking them leads to undefined behavior.
    sigset_t sigset;
    sigfillset(&sigset);
    sigdelset(&sigset, SIGSEGV);
    sigdelset(&sigset, SIGBUS);
    sigdelset(&sigset, SIGFPE);
    sigdelset(&sigset, SIGILL);

    sigset_t prev_sigset;
    rc = pthread_sigmask(SIG_BLOCK, &sigset, &prev_sigset);
    if (rc != 0)
    {
        sem_destroy(&thread_pool->sem);
        free(thread_pool);
        return NULL;
    }
#endif

    pthread_t * thread_id = (pthread_t*) (((char*)thread_pool) + size);
    for (size_t idx=0; idx<worker_threads; idx++)
    {
        // FIXME: handle return code
        pthread_create(
            thread_id,
            NULL,
            &_zv_thread_pool_worker,
            thread_pool
        );
        thread_id++;
    }
    thread_pool->worker_threads = worker_threads;

#if !defined(_WIN32)
    pthread_sigmask(SIG_SETMASK, &prev_sigset, NULL);
#endif

    return thread_pool;
}


void zv_thread_pool_dispatch(
    zv_thread_pool_t * thread_pool,
    struct zv_thread_pool_task_s * task
) {
    assert(task->next == NULL);

#if CONTENTION_FACTOR == 1
    struct zv_thread_pool_partition_s * partition = &thread_pool->partitions[0];
#else
#if defined(_LP64) || defined(__LP64__) || defined(_M_X64)
    const int shift = 3;
#else
    const int shift = 2;
#endif
    size_t idx = ((((uintptr_t)task) >> shift) % CONTENTION_FACTOR);
    struct zv_thread_pool_partition_s * partition = &thread_pool->partitions[idx];
#endif

    struct zv_thread_pool_task_s * tail = atomic_exchange(&partition->tail, task);
    if (tail == NULL)
        atomic_store(&partition->head, task);
    else
        atomic_store(&tail->next, task);

    size_t state = atomic_load(&thread_pool->state);
    for (;;)
    {
        size_t idle_threads = (state & IDLE_THREADS_MASK);
        if (idle_threads == 0)
            break;
        size_t new_state = (state - IDLE_THREAD);
        if (atomic_compare_exchange_strong(&thread_pool->state, &state, new_state))
        {
            sem_post(&thread_pool->sem);
            break;
        }
    }
}


void zv_thread_pool_destroy(zv_thread_pool_t * thread_pool)
{
    size_t state = atomic_load(&thread_pool->state);
    for (;;)
    {
        assert((state & FL_STOP) == 0);
        size_t new_state = (state | FL_STOP);
        if (atomic_compare_exchange_strong(&thread_pool->state, &state, new_state))
            break;
    }

    size_t idle_threads = (state & IDLE_THREADS_MASK);
    for (; idle_threads>0; idle_threads--)
        sem_post(&thread_pool->sem);

    size_t size = _zv_round(sizeof(struct zv_thread_pool_s), sizeof(void*));
    pthread_t * worker_thread_ids = (pthread_t*) (((char*)thread_pool) + size);

    for (size_t idx=0; idx<thread_pool->worker_threads; idx++)
    {
        void * retval;
        pthread_join(worker_thread_ids[idx], &retval);
    }

    sem_destroy(&thread_pool->sem);
    free(thread_pool);
}
