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

#if !defined(ZV_TIMER_MGR_H)
#define ZV_TIMER_MGR_H

#include "zv/export.h"
#include <time.h>

typedef struct zv_thread_pool_s zv_thread_pool_t;
typedef struct zv_timer_mgr_s zv_timer_mgr_t;

#define ZV_ERR_NOMEM -1
#define ZV_TIMER_MGR_ERR_ID_IN_USE -100
#define ZV_TIMER_MGR_ERR_NOT_FOUND -101

/**
 * @brief Creates a timer pool that operates on a specified system clock.
 *
 * On Linux, you can configure a condition variable to use a specific system timer
 * when calling pthread_cond_timedwait(). To enable this functionality, provide
 * the timer identifier via the @p clockid parameter.
 * When using a custom clock, ensure that the @p abstime argument
 * in zv_timer_mgr_at() is set relative to the selected system timer.
 *
 * @param thread_pool Pointer to an existing zv_thread_pool_t that will be used for internal scheduling.
 * @param clockid Pointer to a clockid_t value specifying the system clock to be used for timer operations.
 *                If NULL, the default system clock is used.
 *
 * @return Pointer to the created zv_timer_mgr_t instance on success, or NULL on failure.
 */

ZV_EXPORT zv_timer_mgr_t * zv_timer_mgr_create(
    zv_thread_pool_t * thread_pool,
    const clockid_t * clockid
);

/**
 * @brief Schedules a timer to fire at the specified absolute time.
 *
 * @param timer_mgr Pointer to the zv_timer_mgr_t instance.
 * @param abstime Absolute time at which the timer should fire.
 * @param timer_id Caller-supplied identifier for the timer. Must be unique among
 *                 the currently scheduled timers.
 * @param func Callback invoked when the timer fires.
 *
 * @return 0 on success,
 *         ZV_TIMER_MGR_ERR_ID_IN_USE if a timer with the same @p timer_id is
 *         already scheduled, or
 *         ZV_ERR_NOMEM on allocation failure.
 */

ZV_EXPORT int zv_timer_mgr_at(
    zv_timer_mgr_t * timer_mgr,
    const struct timespec * abstime,
    void * timer_id,
    struct timespec * (*func)(void*, struct timespec*)
);

/**
 * @brief Cancels a previously scheduled timer, waiting for any in-progress
 *        callback to finish before returning.
 *
 * If the timer identified by @p timer_id is currently firing (its callback is
 * being executed by the worker thread), this function blocks until that
 * callback returns, and only then removes the timer. Consequently, once
 * zv_timer_mgr_cancel_sync() returns successfully, the timer's callback is
 * guaranteed neither to be running nor to be invoked again.
 *
 * The call is thread-safe and may be invoked concurrently with
 * zv_timer_mgr_at() and other cancellations.
 *
 * @note Must not be called from within the timer's own callback, as that would
 *       deadlock waiting for the callback to complete.
 * @note For a one-shot timer that is firing while cancelled, the function waits
 *       for the callback to finish and then reports ZV_TIMER_MGR_ERR_NOT_FOUND,
 *       since the timer is no longer scheduled once its callback has run.
 *
 * @param timer_mgr Pointer to the zv_timer_mgr_t instance.
 * @param timer_id Identifier of the timer to cancel, as passed to
 *                 zv_timer_mgr_at().
 *
 * @return 0 on success, or
 *         ZV_TIMER_MGR_ERR_NOT_FOUND if no timer with the given @p timer_id is
 *         currently scheduled.
 */

ZV_EXPORT int zv_timer_mgr_cancel_sync(
    zv_timer_mgr_t * timer_mgr,
    void * timer_id
);

ZV_EXPORT void zv_timer_mgr_destroy(zv_timer_mgr_t * timer_mgr);

#endif /* !defined(ZV_TIMER_MGR_H) */
