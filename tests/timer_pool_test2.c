#include "zv/os_time.h"
#include "zv/thread_pool.h"
#include "zv/timer_pool.h"
#include "zv/semaphore.h"
#include "helpers.h"

// Run 9 timers divided into three groups.
// Timers in each group fire simultaneously.
// In group 1, the first timer fires once and the other timers fire twice.
// In group 2, the second timer fires once and the other timers fire twice.
// In group 3, the third timer fires once and the other timers fire twice.

struct timer_s
{
    sem_t * sem;
    size_t repeat_interval_ms;
};

static struct timespec * timer_func(void * arg, struct timespec * repeat_time)
{
    struct timer_s * timer = arg;
    const size_t repeat_interval_ms = timer->repeat_interval_ms;
    if (repeat_interval_ms > 0)
    {
        timer->repeat_interval_ms = 0;
        timespec_now_adding_ms(repeat_time, repeat_interval_ms);
        return repeat_time;
    }
    else
    {
        sem_post(timer->sem);
        free(timer);
        return NULL;
    }
}

static void create_timers(
    zv_timer_pool_t * timer_pool,
    sem_t * sem,
    int non_repeatable_idx,
    const struct timespec * fire_time
) {
    for (int idx=0; idx<3; idx++)
    {
        struct timer_s * timer = malloc(sizeof(struct timer_s));
        timer->sem = sem;
        timer->repeat_interval_ms = (idx == non_repeatable_idx) ? 0 : 100;
        zv_timer_pool_at(timer_pool, fire_time, timer, &timer_func);
    }
}

static int test2_func(zv_timer_pool_t * timer_pool)
{
    sem_t sem;
    int rc = sem_init(&sem, 0, 0);
    if (rc == 0)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct timespec ts = { tv.tv_sec, tv.tv_usec * 1000 };

        for (int idx=0; idx<3; idx++)
        {
            timespec_add_ms(&ts, 200);
            create_timers(timer_pool, &sem, idx, &ts);
        }

        for (int idx=0; idx<9; idx++)
            sem_wait(&sem);

        sem_destroy(&sem);
    }
    return rc;
}

int timer_pool_test2()
{
    return run_timer_pool_test(&test2_func);
}
