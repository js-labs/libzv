#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include "trace_points.h"

struct test_s
{
    size_t producers;
    size_t count;
    dispatch_queue_t dispatch_queue;

    struct trace_point_s * trace_points;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_cond_t test_done_cond;
    struct timeval start_time;
    size_t producers_ready;
    size_t producers_done;
    struct timeval producers_done_time;
    size_t last_tasks_done;
    struct timeval test_done_time;
};

static struct test_s * test_create(
    size_t producers, size_t count, dispatch_queue_t dispatch_queue)
{
    struct test_s * test = malloc(sizeof(struct test_s));
    test->producers = producers;
    test->count = count;
    test->dispatch_queue = dispatch_queue;
    test->trace_points = malloc(producers*count*sizeof(struct trace_point_s));
    pthread_mutex_init(&test->mutex, NULL);
    pthread_cond_init(&test->cond, NULL);
    pthread_cond_init(&test->test_done_cond, NULL);
    test->producers_ready = 0;
    test->producers_done = 0;
    test->last_tasks_done = 0;
    return test;
}

static void work_func(void * arg)
{
    struct trace_point_s * tp = (struct trace_point_s*) arg;
    tp->thread_id = gettid();
}

struct last_work_s
{
    struct trace_point_s * trace_point;
    struct test_s * test;
};

static void last_work_func(void * arg)
{
    struct last_work_s * last_work = (struct last_work_s*) arg;
    last_work->trace_point->thread_id = gettid();

    struct test_s * test = last_work->test;
    pthread_mutex_lock(&test->mutex);
    if (++test->last_tasks_done == test->producers)
    {
        gettimeofday(&test->test_done_time, NULL);
        pthread_cond_signal(&test->test_done_cond);
    }
    pthread_mutex_unlock(&test->mutex);
    free(last_work);
}

static void * producer(void * arg)
{
    struct test_s * test = (struct test_s*) arg;
    struct last_work_s * last_work = (struct last_work_s*) malloc(sizeof(struct last_work_s));

    pthread_mutex_lock(&test->mutex);
    size_t producer_id = test->producers_ready;
    if (++test->producers_ready == test->producers)
    {
        gettimeofday(&test->start_time, NULL);
        if (test->producers > 1)
            pthread_cond_broadcast(&test->cond);
    }
    else
    {
        while (test->producers_ready < test->producers)
            pthread_cond_wait(&test->cond, &test->mutex);
    }
    pthread_mutex_unlock(&test->mutex);

    struct trace_point_s * trace_point = test->trace_points;
    trace_point += (test->count * producer_id);

    dispatch_queue_global_t dispatch_queue = test->dispatch_queue;

    for (size_t idx=0; idx<(test->count-1); idx++, trace_point++)
        dispatch_async_f(dispatch_queue, trace_point, &work_func);

    last_work->trace_point = trace_point;
    last_work->test = test;
    dispatch_async_f(dispatch_queue, last_work, &last_work_func);

    pthread_mutex_lock(&test->mutex);
    if (++test->producers_done == test->producers)
        gettimeofday(&test->producers_done_time, NULL);
    pthread_mutex_unlock(&test->mutex);

    printf("producer %zd done\n", producer_id);

    return NULL;
}

int gcd_concurrent_queue_throughput()
{
    const size_t PRODUCERS = 5;
    const size_t COUNT = 1000000;

    dispatch_queue_t dispatch_queue = dispatch_queue_create("dq", DISPATCH_QUEUE_CONCURRENT);
    struct test_s * test = test_create(PRODUCERS, COUNT, dispatch_queue);

    pthread_t * thread_id = malloc(sizeof(thread_id) * PRODUCERS);
    for (size_t idx=0; idx<PRODUCERS; idx++)
        pthread_create(&thread_id[idx], NULL, producer, test);

    for (size_t idx=0; idx<PRODUCERS; idx++)
    {
        void * retval;
        pthread_join(thread_id[idx], &retval);
    }

    free(thread_id);

    pthread_mutex_lock(&test->mutex);
    while (test->last_tasks_done != PRODUCERS)
        pthread_cond_wait(&test->test_done_cond, &test->mutex);
    pthread_mutex_unlock(&test->mutex);

    uint64_t t1 = (test->start_time.tv_sec * 1000000 + test->start_time.tv_usec);
    uint64_t t2;
    printf("Producers: %zd\n", PRODUCERS);
    printf("Tasks count: %zd\n", COUNT);

    t2 = (test->producers_done_time.tv_sec * 1000000 + test->producers_done_time.tv_usec);
    printf("Test start -> producers done: %llu usec\n", (t2-t1));

    t2 = (test->test_done_time.tv_sec * 1000000 + test->test_done_time.tv_usec);
    printf("Test start -> last task done: %llu usec\n", (t2-t1));

    trace_points_sum_threads(test->trace_points, COUNT);

    dispatch_release(dispatch_queue);
    free(test->trace_points);
    free(test);

    return 0;
}
