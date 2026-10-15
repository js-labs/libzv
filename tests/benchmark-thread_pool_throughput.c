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

#include "zv/thread_pool.h"
#include "zv/os_thread.h"
#include "zv/os_time.h"
#include <inttypes.h>
#include "helpers.h"

struct test_s
{
    zv_thread_pool_t * thread_pool;

    size_t producers;
    size_t tasks_per_producer;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_cond_t test_done_cond;
    size_t producer_id;
    struct timespec start_time;
    size_t producers_ready;
    size_t producers_done;
    struct timespec producers_done_time;
    size_t last_tasks_done;
    struct timespec test_done_time;
};

struct task_s
{
    struct zv_thread_pool_task_s thread_pool_task;
};

struct last_task_s
{
    struct zv_thread_pool_task_s thread_pool_task;
    struct test_s * test;
};

static int test_init(
    struct test_s * test,
    size_t thread_pool_workers,
    size_t producers,
    size_t tasks_per_producer
) {
    zv_thread_pool_t * thread_pool = zv_thread_pool_create(thread_pool_workers);
    if (thread_pool)
    {
        test->thread_pool = thread_pool;
        test->producers = producers;
        test->tasks_per_producer = tasks_per_producer;
        pthread_mutex_init(&test->mutex, NULL);
        pthread_cond_init(&test->cond, NULL);
        pthread_cond_init(&test->test_done_cond, NULL);
        test->producer_id = 0;
        test->producers_ready = 0;
        test->producers_done = 0;
        test->last_tasks_done = 0;
        return 0;
    }
    else
        return -1;
}

static void test_deinit(struct test_s * test)
{
    zv_thread_pool_destroy(test->thread_pool);
    pthread_cond_destroy(&test->test_done_cond);
    pthread_cond_destroy(&test->cond);
    pthread_mutex_destroy(&test->mutex);
}

static void task_func(struct zv_thread_pool_task_s * arg)
{
    // do nothing here
}

static void last_task_func(struct zv_thread_pool_task_s * arg)
{
    struct last_task_s * last_task = (struct last_task_s*) arg;
    struct test_s * test = last_task->test;
    pthread_mutex_lock(&test->mutex);
    if (++test->last_tasks_done == test->producers)
    {
        // Not 100% correct, because some tasks can be still executed
        // together with the current one, but not more than the number of worker threads.
        // The proper way would be to count all tasks, but it significanlty affect the result
        clock_gettime(CLOCK_MONOTONIC, &test->test_done_time);
        pthread_cond_signal(&test->test_done_cond);
    }
    pthread_mutex_unlock(&test->mutex);
    free(last_task);
}

static void * producer(void * arg)
{
    struct test_s * test = (struct test_s*) arg;

    pthread_mutex_lock(&test->mutex);
    size_t producer_id = test->producer_id++;
    pthread_mutex_unlock(&test->mutex);

    const size_t tasks_per_producer = test->tasks_per_producer;
    struct task_s * tasks = malloc(sizeof(struct task_s) * (tasks_per_producer - 1));
    for (size_t idx=0; idx<(tasks_per_producer-1); idx++)
        zv_thread_pool_task_init(&tasks[idx].thread_pool_task, &task_func);

    struct last_task_s * last_task = malloc(sizeof(struct last_task_s));
    zv_thread_pool_task_init(&last_task->thread_pool_task, &last_task_func);
    last_task->test = test;

    pthread_mutex_lock(&test->mutex);
    if (++test->producers_ready == test->producers)
    {
        clock_gettime(CLOCK_MONOTONIC, &test->start_time);
        if (test->producers > 1)
            pthread_cond_broadcast(&test->cond);
    }
    else
    {
        while (test->producers_ready < test->producers)
            pthread_cond_wait(&test->cond, &test->mutex);
    }
    pthread_mutex_unlock(&test->mutex);

    zv_thread_pool_t * thread_pool = test->thread_pool;

    for (size_t idx=0; idx<(tasks_per_producer-1); idx++)
        zv_thread_pool_dispatch(thread_pool, &tasks[idx].thread_pool_task);

    zv_thread_pool_dispatch(thread_pool, &last_task->thread_pool_task);

    pthread_mutex_lock(&test->mutex);
    size_t producers_done = ++test->producers_done;
    if (producers_done == test->producers)
        clock_gettime(CLOCK_MONOTONIC, &test->producers_done_time);
    pthread_mutex_unlock(&test->mutex);

    printf("Producer %zd done\n", producers_done);

    return NULL;
}


int benchmark_thread_pool_throughput()
{
    const size_t PRODUCERS = 3;
    const size_t TASKS_PER_PRODUCER = (1000 * 1000);

    struct test_s test;
    test_init(&test, 4, PRODUCERS, TASKS_PER_PRODUCER);

    pthread_t * thread_id = malloc(sizeof(pthread_t) * PRODUCERS);
    for (size_t idx=0; idx<PRODUCERS; idx++)
        pthread_create(&thread_id[idx], NULL, producer, &test);

    for (size_t idx=0; idx<PRODUCERS; idx++)
    {
        void * retval;
        pthread_join(thread_id[idx], &retval);
    }

    free(thread_id);

    pthread_mutex_lock(&test.mutex);
    while (test.last_tasks_done != PRODUCERS)
        pthread_cond_wait(&test.test_done_cond, &test.mutex);
    pthread_mutex_unlock(&test.mutex);

    printf("Producers: %zd\n", PRODUCERS);
    printf("Tasks per producer: %zd\n", TASKS_PER_PRODUCER);
    printf(
        "Test start -> producers done: %ld ms\n",
        zv_elapsed_time_us(&test.start_time, &test.producers_done_time) / 1000
    );

    printf(
        "Test start -> last task done: %ld ms\n",
        zv_elapsed_time_us(&test.start_time, &test.test_done_time) / 1000
    );

    test_deinit(&test);
    return 0;
}
