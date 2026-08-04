#include "zv/os_time.h"
#include "zv/thread_pool.h"
#include "zv/timer_mgr.h"
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
    clockid_t clockid;
    long repeat_interval_ms;
};

static struct timespec * timer_func(void * arg, struct timespec * repeat_time)
{
    struct timer_s * timer = arg;
    const long repeat_interval_ms = timer->repeat_interval_ms;
    if (repeat_interval_ms > 0)
    {
        timer->repeat_interval_ms = 0;
        clock_gettime(timer->clockid, repeat_time);
        zv_timespec_add_ms(repeat_time, repeat_interval_ms);
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
    zv_timer_mgr_t * timer_mgr,
    sem_t * sem,
    clockid_t clockid,
    int non_repeatable_idx,
    const struct timespec * fire_time
) {
    for (int idx=0; idx<3; idx++)
    {
        struct timer_s * timer = malloc(sizeof(struct timer_s));
        timer->sem = sem;
        timer->clockid = clockid;
        timer->repeat_interval_ms = (idx == non_repeatable_idx) ? 0 : 100;
        zv_timer_mgr_at(timer_mgr, fire_time, timer, &timer_func);
    }
}

static int test2_func(zv_timer_mgr_t * timer_mgr)
{
    sem_t sem;
    int rc = sem_init(&sem, 0, 0);
    if (rc == 0)
    {
        const clockid_t clockid = zv_timer_mgr_get_clock(timer_mgr);

        struct timespec ts;
        clock_gettime(clockid, &ts);

        for (int idx=0; idx<3; idx++)
        {
            zv_timespec_add_ms(&ts, 200);
            create_timers(timer_mgr, &sem, clockid, idx, &ts);
        }

        for (int idx=0; idx<9; idx++)
            sem_wait(&sem);

        sem_destroy(&sem);
    }
    return rc;
}

int timer_mgr_test2()
{
    return run_timer_mgr_test(&test2_func);
}
