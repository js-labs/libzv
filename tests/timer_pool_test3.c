#include "zv/os_time.h"
#include "zv/thread_pool.h"
#include "zv/timer_pool.h"
#include "zv/semaphore.h"
#include "helpers.h"

// Schedule few timers at the same time then cancel all of them.

struct timer_s
{
    sem_t * sem;
};

static struct timespec * timer_func(void * arg, struct timespec * repeat_time)
{
    struct timer_s * timer = arg;
    sem_post(timer->sem);
    free(timer);
    // printf("timer %p\n", timer);
    return NULL;
}

static struct timespec * timer_func_should_not_be_called(
    void * arg, struct timespec * repeat_time
) {
    abort();
}

static void create_timer(
    zv_timer_pool_t * timer_pool,
    sem_t * sem,
    const struct timespec * fire_time
) {
    struct timer_s * timer = malloc(sizeof(struct timer_s));
    // printf("create_timer %p\n", timer);
    timer->sem = sem;
    zv_timer_pool_at(timer_pool, fire_time, timer, &timer_func);
}

static int test3_func(zv_timer_pool_t * timer_pool)
{
    sem_t sem;
    int rc = sem_init(&sem, 0, 0);
    if (rc == 0)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        int timers[2] = { 1, 2 };
        zv_timespec_add_ms(&ts, 200);
        zv_timer_pool_at(timer_pool, &ts, &timers[0], &timer_func_should_not_be_called);

        create_timer(timer_pool, &sem, &ts);

        zv_timer_pool_at(timer_pool, &ts, &timers[1], &timer_func_should_not_be_called);

        zv_timespec_add_ms(&ts, 200);
        create_timer(timer_pool, &sem, &ts);

        int cancel_rc;
        cancel_rc = zv_timer_pool_cancel_sync(timer_pool, &timers[1]);
        if (cancel_rc != 0)
            rc = -2;

        cancel_rc = zv_timer_pool_cancel_sync(timer_pool, &timers[0]);
        if (cancel_rc != 0)
            rc = -3;

        // expecting 2 timers
        sem_wait(&sem);
        sem_wait(&sem);
        sem_destroy(&sem);
    }
    return rc;
}

int timer_pool_test3()
{
    return run_timer_pool_test(&test3_func);
}
