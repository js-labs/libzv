#include "zv/os_time.h"
#include "zv/thread_pool.h"
#include "zv/timer_mgr.h"
#include "zv/semaphore.h"
#include "helpers.h"
#include <limits.h>

// run timer repeating few times with 200ms interval
#define REPEAT_INTERVAL_MS 200

struct timer_s
{
    sem_t sem;
    int count;
    struct timespec last_time;
    int min_jitter;
    int max_jitter;
};

static int timer_init(struct timer_s * timer, const struct timespec * last_time)
{
    int rc = sem_init(&timer->sem, 0, 0);
    if (rc == 0)
    {
        timer->count = 0;
        timer->last_time = *last_time;
        timer->min_jitter = INT_MAX;
        timer->max_jitter = 0;
    }
    return rc;
}

static void timer_deinit(struct timer_s * timer)
{
    sem_destroy(&timer->sem);
}

static struct timespec * timer_func(void * arg, struct timespec * repeat_time)
{
    struct timer_s * timer = arg;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct timespec ts = { tv.tv_sec, tv.tv_usec * 1000 };
    size_t elapsed_ms = (ts.tv_sec - timer->last_time.tv_sec) * 1000;
    elapsed_ms += (ts.tv_nsec / 1000000);
    elapsed_ms -= (timer->last_time.tv_nsec / 1000000);
    timer->last_time = ts;
    int jitter = (((int)elapsed_ms) - REPEAT_INTERVAL_MS);
    if (jitter < timer->min_jitter)
        timer->min_jitter = jitter;
    if (jitter > timer->max_jitter)
        timer->max_jitter = jitter;
    printf("test_timer1: %zums\n", elapsed_ms);

    if (++timer->count == 5)
        sem_post(&timer->sem);

    zv_timespec_add_ms(&ts, REPEAT_INTERVAL_MS);
    *repeat_time = ts;
    return repeat_time;
}

static int test1_func(zv_timer_mgr_t * timer_mgr)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    struct timer_s timer;
    int rc = timer_init(&timer, &now);
    if (rc == 0)
    {
        struct timespec fire_time = now;
        zv_timespec_add_ms(&fire_time, REPEAT_INTERVAL_MS);
        rc = zv_timer_mgr_at(timer_mgr, &fire_time, &timer, &timer_func);
        if (rc == 0)
        {
            sem_wait(&timer.sem);
            zv_timer_mgr_cancel_sync(timer_mgr, &timer);
            printf("min_jitter=%dms max_jitter=%dms\n", timer.min_jitter, timer.max_jitter);
        }
        else
            printf("zv_timer_mgr_at() failed\n");
        timer_deinit(&timer);
    }
    return rc;
}

int timer_mgr_test1()
{
    return run_timer_mgr_test(&test1_func);
}
