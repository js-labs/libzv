#include "zv/os_time.h"
#include "zv/thread_pool.h"
#include "zv/timer_pool.h"
#include "zv/semaphore.h"
#include "helpers.h"

// Cancel repeating timer

struct timer_s
{
    sem_t sem;
    int ready_to_cancel;
};

static struct timespec * timer_func(void * arg, struct timespec * repeat_time)
{
    struct timer_s * timer = arg;
    if (!timer->ready_to_cancel)
    {
        timer->ready_to_cancel = 1;
        sem_post(&timer->sem);
    }
    timespec_now_adding_ms(repeat_time, 1);
    return repeat_time;
}

static int test4_func(zv_timer_pool_t * timer_pool)
{
    struct timer_s timer;
    int rc = sem_init(&timer.sem, 0, 0);
    if (rc == 0)
    {
        timer.ready_to_cancel = 0;

        struct timespec fire_time;
        timespec_now_adding_ms(&fire_time, 1);
        zv_timer_pool_at(timer_pool, &fire_time, &timer, &timer_func);

        sem_wait(&timer.sem);
        int cancel_rc = zv_timer_pool_cancel_sync(timer_pool, &timer);
        if (cancel_rc != 0)
            rc = -2;

        sem_destroy(&timer.sem);
    }
    return rc;
}

int timer_pool_test4()
{
    return run_timer_pool_test(&test4_func);
}
