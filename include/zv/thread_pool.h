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

#if !defined(ZV_THREAD_POOL_H)
#define ZV_THREAD_POOL_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "zv/export.h"

typedef struct zv_thread_pool_s zv_thread_pool_t;

struct zv_thread_pool_task_s
{
    struct zv_thread_pool_task_s * next;
    void (*func)(struct zv_thread_pool_task_s*);
};

static inline void zv_thread_pool_task_init(
    struct zv_thread_pool_task_s * task,
    void (*func)(struct zv_thread_pool_task_s *)
) {
    task->next = NULL;
    task->func = func;
}

ZV_EXPORT zv_thread_pool_t * zv_thread_pool_create(size_t worker_threads);

ZV_EXPORT void zv_thread_pool_dispatch(
    zv_thread_pool_t * thread_pool,
    struct zv_thread_pool_task_s * task
);

ZV_EXPORT void zv_thread_pool_destroy(zv_thread_pool_t * thread_pool);

#endif /* !defined(ZV_THREAD_POOL_H) */
