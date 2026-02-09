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

#include "zv/os_time.h"
#include "zv/semaphore.h"
#include "zv/thread_pool.h"
#include "histogram.h"
#include "helpers.h"
#include <stdio.h>

struct task_s
{
    struct zv_thread_pool_task_s task;
    struct test_s * test;
};


struct test_s
{
    sem_t sem;
    struct task_s tasks[8];
    struct timespec ts;
    size_t idx;
    size_t count;
    long * stats;
};


static void task_func(struct zv_thread_pool_task_s * arg)
{
    struct task_s * task = (struct task_s*) arg;
    struct test_s * test = task->test;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    test->stats[test->idx] = zv_elapsed_time_us(&test->ts, &ts);
    size_t idx = test->idx++;
    sem_post(&test->sem);
}


static int test_init(struct test_s * test, size_t count)
{
    long * stats = malloc(sizeof(long) * count);
    if (!stats)
        return -1;

    int rc = sem_init(&test->sem, 0, 0);
    if (rc)
    {
        free(stats);
        return -1;
    }

    const size_t tasks_count = sizeof(test->tasks)/sizeof(struct task_s);
    for (int idx=0; idx<tasks_count; idx++)
    {
        zv_thread_pool_task_init(&test->tasks[idx].task, &task_func);
        test->tasks[idx].test = test;
    }

    test->idx = 0;
    test->count = count;
    test->stats = stats;
    return 0;
}


static void test_deinit(struct test_s * test)
{
    free(test->stats);
    sem_destroy(&test->sem);
}


int benchmark_thread_pool_latency()
{
    const size_t COUNT = 1000;

    zv_thread_pool_t * thread_pool = zv_thread_pool_create(2);
    if (!thread_pool)
        return -1;

    struct test_s test;
    int rc = test_init(&test, COUNT);
    if (rc == 0)
    {
        const size_t tasks_count = sizeof(test.tasks)/sizeof(struct task_s);
        for (size_t idx=0; idx<COUNT; idx++)
        {
            const size_t task_idx = (idx % tasks_count);
            clock_gettime(CLOCK_MONOTONIC, &test.ts);
            struct zv_thread_pool_task_s * task = &test.tasks[task_idx].task;
            zv_thread_pool_dispatch(thread_pool, task);
            sem_wait(&test.sem);
        }
        print_histogram(COUNT, test.stats);
        test_deinit(&test);
    }
    zv_thread_pool_destroy(thread_pool);
    return 0;
}
