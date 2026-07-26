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

ZV_EXPORT int zv_timer_mgr_at(
    zv_timer_mgr_t * timer_mgr,
    const struct timespec * tv,
    void * timer_id,
    struct timespec * (*func)(void*, struct timespec*)
);

ZV_EXPORT int zv_timer_mgr_cancel_sync(
    zv_timer_mgr_t * timer_mgr,
    void * timer_id
);

ZV_EXPORT void zv_timer_mgr_destroy(zv_timer_mgr_t * timer_mgr);

#endif /* !defined(ZV_TIMER_MGR_H) */
