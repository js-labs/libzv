#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include "trace_points.h"

#include "uio/semaphore.h"
#include "uio/thread_pool.h"
#include <uio/os_thread.h>
#include "uio/time.h"
#include <stdio.h>
#include "trace_points.h"

struct test_s
{
    dispatch_queue_t dispatch_queue;
    dispatch_semaphore_t sem;
    size_t idx;
    size_t count;
    struct trace_point_s * trace_points;
};

static struct test_s * test_create(dispatch_queue_t dispatch_queue, size_t count)
{
    struct test_s * test = malloc(sizeof(struct test_s));
    test->dispatch_queue = dispatch_queue;
    test->sem = dispatch_semaphore_create(0);
    test->idx = 0;
    test->count = count;
    test->trace_points = malloc(sizeof(struct trace_point_s) * count);
    return test;
}

static void test_destroy(struct test_s * test)
{
    free(test->trace_points);
    dispatch_release(test->sem);
    dispatch_release(test->dispatch_queue);
    free(test);
}

static void work_func(void * arg)
{
    struct test_s * test = (struct test_s*) arg;
    size_t idx = test->idx++;
    if (idx == test->count)
        sem_post(&test->sem);
    else
    {
        struct trace_point_s * trace_points = test->trace_points;
        gettimeofday(&trace_points[idx].tv, NULL);
        trace_points[idx].thread_id = gettid();
        dispatch_async_f(test->dispatch_queue, arg, &work_func);
    }
}

int gcd_concurrent_queue_latency()
{
    const size_t COUNT = 1000;

    dispatch_queue_t dispatch_queue = dispatch_queue_create("dq", DISPATCH_QUEUE_CONCURRENT);
    struct test_s * test = test_create(dispatch_queue, COUNT);

    dispatch_async_f(dispatch_queue, test, work_func);

    dispatch_semaphore_wait(test->sem, DISPATCH_TIME_FOREVER);

    printf("Count = %zd\n", COUNT);
    trace_points_sum_latency(test->trace_points, COUNT);
    trace_points_sum_threads(test->trace_points, COUNT);

    test_destroy(test);

    return 0;
}
