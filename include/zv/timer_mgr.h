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

/**
 * @brief Creates a timer manager that operates on a specified system clock.
 *
 * The clock is a mandatory attribute of the timer manager: it is the clock the
 * internal condition variable waits on (pthread_cond_timedwait()), and hence
 * the clock every absolute time passed to or returned from the manager is
 * interpreted against.
 *
 * @warning All absolute times handed to the manager must be calculated with
 *          the very same clock, i.e. with
 *          clock_gettime(zv_timer_mgr_get_clock(timer_mgr), ...). This applies
 *          to the @p abstime argument of zv_timer_mgr_at() as well as to the
 *          repeat time returned by a timer callback. Mixing clocks (for
 *          example computing a fire time with gettimeofday() while the manager
 *          runs on CLOCK_MONOTONIC) makes timers fire at arbitrary moments,
 *          usually immediately, because the two time scales differ by the
 *          system boot time.
 *
 * Platform support:
 *   - Linux: any clock accepted by pthread_condattr_setclock(), which in
 *     practice means CLOCK_REALTIME or CLOCK_MONOTONIC. CLOCK_MONOTONIC is
 *     the recommended choice, as it is not affected by wall clock adjustments
 *     (NTP steps, clock_settime()); note that it does not advance while the
 *     system is suspended.
 *   - macOS: only CLOCK_REALTIME is supported, since the platform provides no
 *     way to select the clock of a condition variable. Any other value makes
 *     this function fail.
 *
 * @param thread_pool Pointer to an existing zv_thread_pool_t that will be used for internal scheduling.
 * @param clockid System clock to be used for all timer operations.
 *
 * @return Pointer to the created zv_timer_mgr_t instance on success, or NULL on failure,
 *         including the case of a clock not supported on the current platform.
 */

ZV_EXPORT zv_timer_mgr_t * zv_timer_mgr_create(
    zv_thread_pool_t * thread_pool,
    clockid_t clockid
);

ZV_EXPORT clockid_t zv_timer_mgr_get_clock(zv_timer_mgr_t * timer_mgr);

/**
 * @brief Schedules a timer to fire at the specified absolute time.
 *
 * @p abstime must be calculated using the clock the timer manager was created
 * with, as returned by zv_timer_mgr_get_clock(); see zv_timer_mgr_create().
 * A time already in the past makes the timer fire as soon as possible.
 *
 * @param timer_mgr Pointer to the zv_timer_mgr_t instance.
 * @param abstime Absolute time at which the timer should fire, expressed on the
 *                clock returned by zv_timer_mgr_get_clock().
 * @param timer_id Caller-supplied identifier for the timer. Must be unique among
 *                 the currently scheduled timers.
 * @param func Callback invoked when the timer fires. To repeat the timer, the
 *             callback stores the next absolute fire time in the timespec it
 *             receives and returns that pointer; returning NULL makes the timer
 *             one-shot. The repeat time must be expressed on the same clock as
 *             @p abstime.
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
