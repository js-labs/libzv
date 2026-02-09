#include "zv/log.h"
#include "zv/os_time.h"
#include "zv/thread_pool.h"
#include "zv/timer_pool.h"
#include "helpers.h"
#include <assert.h>
#include <string.h>
#include <inttypes.h>

static struct zv_log_layer_s test_log = { "timer_pool_test5", zv_log_level_debug };

struct test_s;

struct timer_s
{
    struct test_s * test;
    struct timespec ts;
    int cancel;
};

struct test_s
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    size_t count;
    struct timer_s timers[5000];
};

// return random number in the range [0, max)
static int randr(int max)
{
    const int limit = (RAND_MAX - (RAND_MAX % max));
    for (;;)
    {
        const int rnd = rand();
        if (rnd <= limit)
            return (rnd % max);
    }
}

static unsigned int popcount(unsigned char v)
{
    unsigned int result = v;
    result = (result & 0x55) + ((result >> 1) & 0x55);
    result = (result & 0x33) + ((result >> 2) & 0x33);
    result = (result & 0x0F) + ((result >> 4) & 0x0F);
    assert(result <= 8);
    return result;
}

static struct timespec * get_rand_time(
    const struct timer_s * timers,
    size_t count,
    int existing,
    struct timespec * ts /*in/out*/
) {
    unsigned char set[125];
    memset(set, 0, sizeof(set));
    unsigned int free_slots = 1000;
    for (size_t idx=0; idx<count; idx++)
    {
        const struct timespec * fire_time = &timers[idx].ts;
        if (fire_time->tv_sec == 0) 
            continue;
        if ((fire_time->tv_sec > ts->tv_sec) ||
            (fire_time->tv_sec == ts->tv_sec) && (fire_time->tv_nsec >= ts->tv_nsec))
        {
            if ((fire_time->tv_nsec % (1000 * 1000)) != 0)
            {
                ZV_LOG_ERROR(test_log,
                    "internal error: timer 0x%" PRIxPTR " has invalid time %ld.%.9ld",
                    &timers[idx],
                    fire_time->tv_sec,
                    fire_time->tv_nsec
                );
                zv_log_flush();
                abort();
            }
            const long delay = zv_elapsed_time_us(ts, fire_time) / 1000;
            if (delay < 1000)
            {
                unsigned char * ptr = &set[delay / 8];
                const unsigned char mask = (1 << (delay % 8));
                if ((*ptr & mask) == 0)
                {
                    *ptr |= mask;
                    free_slots--;
                }
            }
        }
    }

    if (existing)
    {
        free_slots = (1000 - free_slots);
        for (size_t idx=0; idx<sizeof(set); idx++)
            set[idx] = ~set[idx];
    }

    if (free_slots > 1)
    {
        unsigned int slot_idx = randr(free_slots);
        unsigned int delay_ms = 0;
        const unsigned char * ptr = set;
        for (size_t idx=0; idx<sizeof(set); idx++, ptr++)
        {
            const unsigned int zbc = (8 - popcount(*ptr));
            if (slot_idx < zbc)
            {
                for (unsigned char mask=1; mask!=0; mask<<=1)
                {
                    if ((*ptr & mask) == 0)
                    {
                        if (slot_idx == 0)
                        {
                            timespec_add_ms(ts, delay_ms);
                            return ts;
                        }
                        slot_idx--;
                    }
                    delay_ms++;
                }
                ZV_LOG_ERROR(test_log, "internal error");
                zv_log_flush();
                abort();
                return NULL;
            }
            delay_ms += 8;
            slot_idx -= zbc;
        }
        ZV_LOG_ERROR(test_log, "internal error");
        zv_log_flush();
        abort();
        return NULL;
    }
    else if (free_slots == 1)
    {
        unsigned int delay_ms = 0;
        const unsigned char * ptr = set;
        for (size_t idx=0; idx<sizeof(set); idx++, ptr++)
        {
            if (*ptr == 0xFF)
            {
                delay_ms += 8;
                continue;
            }
            for (unsigned char mask=1; mask!=0; mask<<=1)
            {
                if ((*ptr & mask) == 0)
                {
                    timespec_add_ms(ts, delay_ms);
                    return ts;
                }
                delay_ms++;
            }
        }
        ZV_LOG_ERROR(test_log, "internal error");
        zv_log_flush();
        abort();
        return NULL;
    }
    else
    {
        // can happen sometimes
        return NULL;
    }
}

static struct timer_s * find_timer(struct timer_s * timers, size_t count, struct timespec * ts)
{
    struct timer_s * timer = timers;
    for (size_t idx=0; idx<count; idx++)
    {
        if ((timer->ts.tv_sec == ts->tv_sec)
            && (timer->ts.tv_nsec == ts->tv_nsec))
        {
            return timer;
        }
        timer++;
    }
    return NULL;
}

static struct timespec * timer_func(void * arg, struct timespec * repeat_time)
{
    struct timer_s * timer = arg;
    struct test_s * test = timer->test;

    const int rnd = rand();
    pthread_mutex_lock(&test->mutex);

    ZV_LOG_DEBUG(
        test_log,
        "timer_func: 0x%" PRIxPTR " %zu.%.3zu%s",
        (uintptr_t) timer,
        timer->ts.tv_sec,
        timer->ts.tv_nsec / (1000 * 1000),
        timer->cancel ? " /cancelled" : ""
    );

    if (timer->cancel)
    {
        timer->ts.tv_sec = 0;
        test->count--;
        pthread_mutex_unlock(&test->mutex);
        return NULL;
    }

    struct timespec now = timer->ts;
    timer->ts.tv_sec = 0;
    if (rnd < (RAND_MAX/3))
    {
        // repeat timer
        const size_t count = (sizeof(test->timers) / sizeof(test->timers[0]));
        *repeat_time = now;
        if (rnd < (RAND_MAX/6))
        {
            // new time in the nearest second
            repeat_time = get_rand_time(test->timers, count, /*existing*/0, repeat_time);
            if (repeat_time != NULL)
            {
                ZV_LOG_DEBUG(test_log,
                    "repeat @ new time: %zu.%.3zu",
                    repeat_time->tv_sec,
                    repeat_time->tv_nsec / (1000 * 1000)
                );

                // sanity check the time is really new
                if (find_timer(test->timers, count, repeat_time) != NULL)
                {
                    ZV_LOG_ERROR(test_log, "internal error");
                    zv_log_flush();
                    abort();
                }
            }
        }
        else
        {
            // some existing time
            repeat_time = get_rand_time(test->timers, count, /*existing*/1, repeat_time);
            if (repeat_time != NULL)
            {
                ZV_LOG_DEBUG(test_log,
                    "repeat @ existing time: %zu.%.3zu",
                    repeat_time->tv_sec,
                    repeat_time->tv_nsec / (1000 * 1000)
                );

                // sanity check the time is existing
                if (find_timer(test->timers, count, repeat_time) == NULL)
                {
                    ZV_LOG_ERROR(test_log, "internal error");
                    zv_log_flush();
                    abort();
                }
            }
        }

        if (repeat_time == NULL)
            test->count--;
        else
            timer->ts = *repeat_time;
    }
    else
    {
        test->count--;
        repeat_time = NULL;
    }
    pthread_mutex_unlock(&test->mutex);
    return repeat_time;
}

static int test_init(struct test_s * test)
{
    int rc = pthread_mutex_init(&test->mutex, NULL);
    if (rc == 0)
    {
        rc = pthread_cond_init(&test->cond, NULL);
        if (rc == 0)
        {
            test->count = 0;
            return 0;
        }
        pthread_mutex_destroy(&test->mutex);
    }
    return -1;
}

static void test_deinit(struct test_s * test)
{
    pthread_cond_destroy(&test->cond);
    pthread_mutex_destroy(&test->mutex);
}

static int test5_func(zv_timer_pool_t * timer_pool)
{
    struct test_s test;
    int rc = test_init(&test);
    if (rc != 0)
        return rc;

    const size_t count = (sizeof(test.timers) / sizeof(test.timers[0]));
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_sec);

    // schedule all timers
    for (size_t idx=0; idx<count; idx++)
    {
        const int delay = randr(1000);
        struct timespec ts = { tv.tv_sec, (tv.tv_usec - (tv.tv_usec % 1000)) * 1000 };
        timespec_add_ms(&ts, delay);
        struct timer_s * timer = &test.timers[idx];
        timer->test = &test;
        timer->ts = ts;
        timer->cancel = 0;
        ZV_LOG_DEBUG(test_log,
            "schedule timer 0x%" PRIxPTR " @ %ld.%.3ld",
            (uintptr_t) &test.timers[idx],
            ts.tv_sec,
            ts.tv_nsec / (1000 * 1000)
        );
    }

    test.count = count;
    for (size_t idx=0; idx<count; idx++)
    {
        struct timer_s * timer = &test.timers[idx];
        int rc = zv_timer_pool_at(timer_pool, &timer->ts, timer, timer_func);
        if (rc != 0)
        {
            ZV_LOG_ERROR(test_log,
                "internal_error: zv_timer_pool_at(0x%" PRIxPTR ") failed",
                (uintptr_t)timer
            );
            zv_log_flush();
            abort();
        }
    }

    for (;;)
    {
        const int sleep_time_ms = randr(10);
        usleep(sleep_time_ms * 1000);

        int rnd = rand();

        pthread_mutex_lock(&test.mutex);
        if (test.count == 0)
        {
            pthread_mutex_unlock(&test.mutex);
            break;
        }

        if (rnd < ((RAND_MAX)/2))
        {
            // start new timer
            struct timer_s * timer = NULL;
            if (test.count < count)
            {
                timer = test.timers;
                size_t idx = 0;
                for (; idx<count; idx++)
                {
                    if (timer->ts.tv_sec == 0)
                        break;
                    timer++;
                }

                if (idx == count)
                {
                    // internal error, should not happen
                    ZV_LOG_ERROR(test_log, "internal error");
                    zv_log_flush();
                    abort();
                }

                gettimeofday(&tv, NULL);
                struct timespec ts = { tv.tv_sec, (tv.tv_usec / 1000) * (1000 * 1000) };

                const int existing = (rnd < ((RAND_MAX)/4));
                struct timespec * fire_time = get_rand_time(
                    test.timers,
                    test.count,
                    existing,
                    &ts
                );

                if (fire_time == NULL)
                {
                    // can happen if should cancel existing timer,
                    // but there are no timers in the nearest second
                    timer = NULL;
                }
                else
                {
                    timer->ts = ts;
                    timer->cancel = 0;
                    test.count++;
                }
            }
            pthread_mutex_unlock(&test.mutex);

            if (timer != NULL)
            {
                ZV_LOG_DEBUG(test_log,
                    "schedule timer %p at %ld.%.3ld",
                    timer,
                    timer->ts.tv_sec,
                    timer->ts.tv_nsec / (1000 * 1000)
                );
                rc = zv_timer_pool_at(timer_pool, &timer->ts, timer, timer_func);
                if (rc != 0)
                {
                    // should not happen
                    ZV_LOG_ERROR(test_log, "internal error: zv_timer_pool_at() failed");
                    zv_log_flush();
                    abort();
                }
            }
        }
        else
        {
            // cancel random existing timer
            int timer_idx = randr((int)test.count);
            struct timer_s * timer = test.timers;
            size_t idx = 0;
            for (; idx<count; idx++)
            {
                if (timer->ts.tv_sec != 0)
                {
                    if (timer_idx == 0)
                    {
                        timer->cancel = 1;
                        break;
                    }
                    timer_idx--;
                }
                timer++;
            }

            if (idx == count)
            {
                // internal error, should not happen
                ZV_LOG_ERROR(test_log, "internal error");
                zv_log_flush();
                abort();
            }

            pthread_mutex_unlock(&test.mutex);
            if (timer != NULL)
            {
                int rc = zv_timer_pool_cancel_sync(timer_pool, timer);
                if (rc == 0)
                {
                    pthread_mutex_lock(&test.mutex);
                    timer->ts.tv_sec = 0;
                    test.count--;
                    pthread_mutex_unlock(&test.mutex);
                }
                else
                {
                    // can happen if the timer pool worker is calling
                    // timer function for that timer right now
                    // printf("zv_timer_pool_cancel_sync(%p) failed\n", timer);
                }
            }
        }
    }

    test_deinit(&test);
    return 0;
}

int timer_pool_test5()
{
    return run_timer_pool_test(&test5_func);
}
