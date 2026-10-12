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

#include "zv/err.h"
#include "zv/semaphore.h"
#include "zv/signal.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

struct handler_s
{
    sem_t sem;
    atomic_int count;
};

static int handler_init(struct handler_s * handler)
{
    int rc = sem_init(&handler->sem, 0, 0);
    atomic_init(&handler->count, 0);
    return rc;
}

static void handler1(void * thunk)
{
    abort();
}

static void handler2(void * thunk)
{
    struct handler_s * handler = thunk;
    atomic_fetch_add(&handler->count, 1);
    sem_post(&handler->sem);
}

static void handler3(void * thunk)
{
    struct handler_s * handler = thunk;
    atomic_fetch_add(&handler->count, 1);
    sem_post(&handler->sem);
}

static void self_remove_handler(void * thunk)
{
    struct handler_s * handler = thunk;
    const int rc = zv_signal_remove_handler(&self_remove_handler);
    if (rc == 0)
        atomic_fetch_add(&handler->count, 1);
    sem_post(&handler->sem);
}

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr); \
            return -1; \
        } \
    } while (0)

int signal1()
{
    struct handler_s h2, h3, srh;
    CHECK(handler_init(&h2) == 0);
    CHECK(handler_init(&h3) == 0);
    CHECK(handler_init(&srh) == 0);

    // two handlers for the SIGUSR1
    CHECK(zv_signal_add_handler(SIGUSR1, &handler1, NULL) == 0);
    CHECK(zv_signal_add_handler(SIGUSR1, &handler1, NULL) == ZV_ERR_INVAL);
    CHECK(zv_signal_add_handler(SIGUSR1, &handler2, &h2) == 0);
    CHECK(zv_signal_add_handler(SIGUSR1, &handler3, &h3) == 0);
    CHECK(zv_signal_add_handler(SIGUSR1, &self_remove_handler, &srh) == 0);
    CHECK(zv_signal_remove_handler(&handler1) == 0);
    CHECK(zv_signal_remove_handler(&handler1) == ZV_ERR_INVAL);

    kill(getpid(), SIGUSR1);
    sem_wait(&h2.sem);
    sem_wait(&h3.sem);
    sem_wait(&srh.sem);

    CHECK(zv_signal_remove_handler(&handler2) == 0);
    CHECK(zv_signal_remove_handler(&handler3) == 0);
    CHECK(zv_signal_remove_handler(&self_remove_handler) == ZV_ERR_INVAL);

    CHECK(atomic_load(&h2.count) == 1);
    CHECK(atomic_load(&h3.count) == 1);
    CHECK(atomic_load(&srh.count) == 1);

    sem_destroy(&h2.sem);
    sem_destroy(&h3.sem);
    sem_destroy(&srh.sem);

    return 0;
}
