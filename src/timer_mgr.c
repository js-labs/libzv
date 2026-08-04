/*
 * Copyright (C) 2022 Sergey Zubarev, info@js-labs.org
 *
 * This file is a part of UIO framework.
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

#include "zv/err.h"
#include "zv/os_thread.h"
#include "zv/os_time.h"
#include "zv/thread_pool.h"
#include "zv/timer_mgr.h"
#include "misc.h"
#include "buffer_set/buffer_set.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/time.h>
#endif

struct zv_timer_mgr_s
{
    zv_thread_pool_t * thread_pool;
    clockid_t clockid;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    buffer_set_t * timer_by_id;
    buffer_set_t * timer_by_fire_time;
    char worker;
    char firing_timer;
    void * firing_timer_id;
    int waiters;
    struct zv_thread_pool_task_s task;
};


struct zv_timer_mgr_timer_info_s
{
    void * timer_id;
    struct timespec fire_time;
    struct timespec * (*func)(void*, struct timespec*);
};


struct zv_timer_mgr_fire_time_item_s
{
    struct zv_timer_mgr_fire_time_item_s * prev;
    struct zv_timer_mgr_fire_time_item_s * next;
    void * timer_id;
};


struct zv_timer_mgr_fire_time_s
{
    struct timespec abstime;
    struct zv_timer_mgr_fire_time_item_s head;
    struct zv_timer_mgr_fire_time_item_s * tail;
};


static int _zv_timer_mgr_timer_id_cmp_cb(const void * v1, const void * v2, void * thunk)
{
    const struct zv_timer_mgr_timer_info_s * t1 = v1;
    const struct zv_timer_mgr_timer_info_s * t2 = v2;
    if (t1->timer_id < t2->timer_id)
        return -1;
    else if (t2->timer_id < t1->timer_id)
        return 1;
    else
        return 0;
}


static int _zv_timer_mgr_fire_time_cmp_cb(const void * v1, const void * v2, void * thunk)
{
    const struct zv_timer_mgr_fire_time_s * ft1 = v1;
    const struct zv_timer_mgr_fire_time_s * ft2 = v2;
    if (ft1->abstime.tv_sec < ft2->abstime.tv_sec)
        return -1;
    else if (ft2->abstime.tv_sec < ft1->abstime.tv_sec)
        return 1;
    else
    {
        if (ft1->abstime.tv_nsec < ft2->abstime.tv_nsec)
            return -1;
        else if (ft2->abstime.tv_nsec < ft1->abstime.tv_nsec)
            return 1;
        else
            return 0;
    }
}


static inline void _zv_timer_mgr_fire_time_move(
    struct zv_timer_mgr_fire_time_s * dst,
    const struct zv_timer_mgr_fire_time_s * src
) {
    dst->abstime = src->abstime;
    dst->head = src->head;
    if (src->tail == &src->head)
        dst->tail = &dst->head;
    else
    {
        dst->tail = src->tail;
        dst->head.next->prev = &dst->head;
    }
}


static void _zv_timer_mgr_fire_time_move_cb(void * dst, void * src, void * thunk)
{
    _zv_timer_mgr_fire_time_move(dst, src);
}


static int _zv_timer_mgr_insert_locked(
    zv_timer_mgr_t * timer_mgr,
    const struct timespec * abstime,
    void * timer_id,
    struct timespec * (*func)(void*, struct timespec*)
) {
    int inserted;
    struct zv_timer_mgr_timer_info_s * timer_info;
    timer_info = buffer_set_insert(timer_mgr->timer_by_id, &timer_id, &inserted);
    if (timer_info == NULL)
        return ZV_ERR_NOMEM;
    else if (!inserted)
        return ZV_TIMER_MGR_ERR_ID_IN_USE;

    struct zv_timer_mgr_fire_time_s * fire_time;
    fire_time = buffer_set_insert(timer_mgr->timer_by_fire_time, abstime, &inserted);
    if (fire_time == NULL)
    {
        buffer_set_erase(timer_mgr->timer_by_id, &timer_id);
        return ZV_ERR_NOMEM;
    }

    timer_info->timer_id = timer_id;
    timer_info->func = func;
    timer_info->fire_time = *abstime;

    if (inserted)
    {
        fire_time->abstime = *abstime;
        fire_time->head.prev = NULL;
        fire_time->head.next = NULL;
        fire_time->head.timer_id = timer_id;
        fire_time->tail = &fire_time->head;
    }
    else
    {
        struct zv_timer_mgr_fire_time_item_s * fire_time_item;
        fire_time_item = malloc(sizeof(struct zv_timer_mgr_fire_time_item_s));
        if (fire_time_item == NULL)
        {
            buffer_set_erase(timer_mgr->timer_by_id, &timer_id);
            return ZV_ERR_NOMEM;
        }

        fire_time_item->timer_id = timer_id;
        fire_time_item->prev = fire_time->tail;
        fire_time_item->next = NULL;
        fire_time->tail->next = fire_time_item;
        fire_time->tail = fire_time_item;
    }

    return 0;
}


static void _zv_timer_mgr_worker(struct zv_thread_pool_task_s * task)
{
    struct zv_timer_mgr_s * timer_mgr = _ZV_CONTAINER_OF(task, struct zv_timer_mgr_s, task);
    pthread_mutex_lock(&timer_mgr->mutex);
    for (;;)
    {
        struct timespec now;
        clock_gettime(timer_mgr->clockid, &now);

        buffer_set_iterator_t * it = buffer_set_begin(timer_mgr->timer_by_fire_time);
        buffer_set_iterator_t * it_end = buffer_set_end(timer_mgr->timer_by_fire_time);
        struct zv_timer_mgr_fire_time_s * fire_time;

        while (it != it_end)
        {
            fire_time = buffer_set_get_at(timer_mgr->timer_by_fire_time, it);

            if (fire_time->abstime.tv_sec > now.tv_sec)
                break;
            else if (fire_time->abstime.tv_sec == now.tv_sec)
            {
                if (fire_time->abstime.tv_nsec > now.tv_nsec)
                    break;
            }

            void * timer_id = fire_time->tail->timer_id;
            if (&fire_time->head == fire_time->tail)
                buffer_set_erase_at(timer_mgr->timer_by_fire_time, it);
            else
            {
                struct zv_timer_mgr_fire_time_item_s * tail = fire_time->tail;
                tail->prev->next = NULL;
                fire_time->tail = tail->prev;
                free(tail);
            }

            struct zv_timer_mgr_timer_info_s * timer_info;
            timer_info = buffer_set_erase(timer_mgr->timer_by_id, &timer_id);
            if (timer_info == NULL)
                abort();

            struct timespec * (*func)(void*, struct timespec*) = timer_info->func;
            timer_mgr->firing_timer = 1;
            timer_mgr->firing_timer_id = timer_id;

            pthread_mutex_unlock(&timer_mgr->mutex);

            struct timespec repeat_time_storage;
            const struct timespec * repeat_time = func(timer_id, &repeat_time_storage);

            pthread_mutex_lock(&timer_mgr->mutex);

            if (repeat_time != NULL)
                _zv_timer_mgr_insert_locked(timer_mgr, repeat_time, timer_id, func);

            it = buffer_set_begin(timer_mgr->timer_by_fire_time);
            it_end = buffer_set_end(timer_mgr->timer_by_fire_time);
        }

        timer_mgr->firing_timer = 0;

        if (it == it_end)
            break;

        fire_time = buffer_set_get_at(timer_mgr->timer_by_fire_time, it);

        if (timer_mgr->waiters > 0)
            pthread_cond_broadcast(&timer_mgr->cond);

        pthread_cond_timedwait(&timer_mgr->cond, &timer_mgr->mutex, &fire_time->abstime);
    }

    timer_mgr->worker = 0;
    if (timer_mgr->waiters > 0)
        pthread_cond_signal(&timer_mgr->cond);

    pthread_mutex_unlock(&timer_mgr->mutex);
}


static int _zv_timer_mgr_init_mutex_and_cond(
    pthread_mutex_t * mutex,
    pthread_cond_t * cond,
    clockid_t clockid
) {
    int rc = pthread_mutex_init(mutex, NULL);
    if (rc == 0)
    {
        pthread_condattr_t condattr;
        rc = pthread_condattr_init(&condattr);
        if (rc == 0)
        {
#if defined(__linux__)
            rc = pthread_condattr_setclock(&condattr, clockid);
#elif defined(__APPLE__)
            rc = (clockid == CLOCK_REALTIME) ? 0 : -1;
#elif defined(_WIN32)
            rc = ((clockid == CLOCK_REALTIME) || (clockid == CLOCK_MONOTONIC)) ? 0 : -1;
#else
#error Platform not supported
#endif
            if (rc == 0)
            {
                rc = pthread_cond_init(cond, &condattr);
                if (rc == 0)
                {
                    pthread_condattr_destroy(&condattr);
                    return 0;
                }
            }
            pthread_condattr_destroy(&condattr);
        }
        pthread_mutex_destroy(mutex);
    }
    return rc;
}


zv_timer_mgr_t * zv_timer_mgr_create(
    zv_thread_pool_t * thread_pool,
    clockid_t clockid
) {
    struct zv_timer_mgr_s * timer_mgr = malloc(sizeof(struct zv_timer_mgr_s));
    if (timer_mgr == NULL)
        return NULL;

    buffer_set_t * timer_by_id = buffer_set_create(
        sizeof(struct zv_timer_mgr_timer_info_s),
        0, // initial capacity
        &_zv_timer_mgr_timer_id_cmp_cb,
        NULL, // move func
        NULL  // thunk
    );

    if (timer_by_id != NULL)
    {
        buffer_set_t * timer_by_fire_time = buffer_set_create(
            sizeof(struct zv_timer_mgr_fire_time_s),
            0, // initial capacity
            &_zv_timer_mgr_fire_time_cmp_cb,
            &_zv_timer_mgr_fire_time_move_cb,
            NULL
        );

        if (timer_by_fire_time != NULL)
        {
            int rc = _zv_timer_mgr_init_mutex_and_cond(
                &timer_mgr->mutex,
                &timer_mgr->cond,
                clockid
            );

            if (rc == 0)
            {
                timer_mgr->thread_pool = thread_pool;
                timer_mgr->clockid = clockid;
                timer_mgr->timer_by_id = timer_by_id;
                timer_mgr->timer_by_fire_time = timer_by_fire_time;
                timer_mgr->worker = 0;
                timer_mgr->firing_timer = 0;
                timer_mgr->waiters = 0;
                zv_thread_pool_task_init(&timer_mgr->task, &_zv_timer_mgr_worker);
                return timer_mgr;
            }
            buffer_set_destroy(timer_by_fire_time);
        }
        buffer_set_destroy(timer_by_id);
    }
    free(timer_mgr);
    return NULL;
}


clockid_t zv_timer_mgr_get_clock(zv_timer_mgr_t * timer_mgr)
{
    return timer_mgr->clockid;
}


void zv_timer_mgr_destroy(zv_timer_mgr_t * timer_mgr)
{
    pthread_mutex_lock(&timer_mgr->mutex);
    assert(buffer_set_get_size(timer_mgr->timer_by_fire_time) == 0);
    assert(buffer_set_get_size(timer_mgr->timer_by_id) == 0);
    if (timer_mgr->worker)
    {
        timer_mgr->waiters++;
        do
        {
            pthread_cond_wait(&timer_mgr->cond, &timer_mgr->mutex);
        }
        while (timer_mgr->worker);
        timer_mgr->waiters--;
    }
    pthread_mutex_unlock(&timer_mgr->mutex);

    buffer_set_destroy(timer_mgr->timer_by_fire_time);
    buffer_set_destroy(timer_mgr->timer_by_id);
    pthread_cond_destroy(&timer_mgr->cond);
    pthread_mutex_destroy(&timer_mgr->mutex);
    free(timer_mgr);
}

/*
static void _value_printer(FILE * file, const void * value)
{
    fprintf(file, "%p", *(const void**)value);
}
*/

int zv_timer_mgr_at(
    zv_timer_mgr_t * timer_mgr,
    const struct timespec * abstime,
    void * timer_id,
    struct timespec * (*func)(void*, struct timespec*)
) {
    pthread_mutex_lock(&timer_mgr->mutex);

    const int start_worker =
        ((buffer_set_get_size(timer_mgr->timer_by_id) == 0)
         && (timer_mgr->firing_timer == 0));

    int rc = _zv_timer_mgr_insert_locked(timer_mgr, abstime, timer_id, func);
    if (rc == 0)
    {
        if (start_worker)
        {
            timer_mgr->worker = 1;
            zv_thread_pool_dispatch(timer_mgr->thread_pool, &timer_mgr->task);
        }
        else
        {
            buffer_set_iterator_t * it = buffer_set_begin(timer_mgr->timer_by_fire_time);
            struct zv_timer_mgr_fire_time_s * fire_time;
            fire_time = buffer_set_get_at(timer_mgr->timer_by_fire_time, it);
            if ((fire_time->abstime.tv_sec == abstime->tv_sec) &&
                (fire_time->abstime.tv_nsec == abstime->tv_nsec) &&
                (&fire_time->head == fire_time->tail))
            {
                pthread_cond_signal(&timer_mgr->cond);
            }
        }
    }

    pthread_mutex_unlock(&timer_mgr->mutex);

    return rc;
}


int zv_timer_mgr_cancel_sync(
    zv_timer_mgr_t * timer_mgr,
    void * timer_id
) {
    int rc = ZV_TIMER_MGR_ERR_NOT_FOUND;
    pthread_mutex_lock(&timer_mgr->mutex);
    if (timer_mgr->firing_timer && (timer_mgr->firing_timer_id == timer_id))
    {
        timer_mgr->waiters++;
        do
        {
            pthread_cond_wait(&timer_mgr->cond, &timer_mgr->mutex);
        }
        while (timer_mgr->firing_timer && (timer_mgr->firing_timer_id == timer_id));
        timer_mgr->waiters--;
    }

    struct zv_timer_mgr_timer_info_s * timer_info;
    timer_info = buffer_set_erase(timer_mgr->timer_by_id, &timer_id);
    if (timer_info != NULL)
    {
        buffer_set_iterator_t * it;
        it = buffer_set_find(timer_mgr->timer_by_fire_time, &timer_info->fire_time);
        if (it == buffer_set_end(timer_mgr->timer_by_fire_time))
            assert(0);
        else
        {
            struct zv_timer_mgr_fire_time_s * fire_time;
            fire_time = buffer_set_get_at(timer_mgr->timer_by_fire_time, it);

            struct zv_timer_mgr_fire_time_item_s * item = fire_time->tail;
            while (item != NULL)
            {
                if (item->timer_id != timer_id)
                {
                    item = item->prev;
                    continue;
                }

                if (item == &fire_time->head)
                {
                    struct zv_timer_mgr_fire_time_item_s * next = item->next;
                    if (next == NULL)
                    {
                        // there is only one item
                        buffer_set_erase_at(timer_mgr->timer_by_fire_time, it);

                        // Not sure it makes sense to wake up the worker
                        // if there are more timers and the cancelled timer was the first.
                        if (buffer_set_get_size(timer_mgr->timer_by_id) == 0)
                            pthread_cond_signal(&timer_mgr->cond);
                    }
                    else
                    {
                        fire_time->head.next = next->next;
                        fire_time->head.timer_id = next->timer_id;
                        if (next->next == NULL)
                            fire_time->tail = &fire_time->head;
                        else
                            next->next->prev = &fire_time->head;
                        free(next);
                    }
                }
                else
                {
                    item->prev->next = item->next;
                    if (item->next == NULL)
                        fire_time->tail = item->prev;
                    else
                        item->next->prev = item->prev;
                    free(item);
                }
                rc = 0;
                break;
            }
        }
    }
    pthread_mutex_unlock(&timer_mgr->mutex);
    return rc;
}
