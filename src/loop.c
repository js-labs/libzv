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

#include "zv/log.h"
#include "zv/loop.h"

#include "loop_int.h"
#include "misc.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

#if defined(__linux__)
#include <unistd.h>
#endif

struct zv_log_layer_s _zv_loop_log = { "xv.loop", zv_log_level_info };

const char * zv_event_type_get_name(zv_event_type_t event_type)
{
    switch (event_type)
    {
        case zv_event_type_accept:
            return "ACCEPT";
        case zv_event_type_read:
            return "READ";
        case zv_event_type_connect:
            return "CONNECT";
        case zv_event_type_write:
            return "WRITE";
        default:
            assert(0);
            return "UNKNOWN";
    }
}

const char * zv_event_opcode_get_name(zv_event_opcode_t event_opcode)
{
    switch (event_opcode)
    {
        case zv_event_opcode_add:
            return "ADD";
        case zv_event_opcode_resume:
            return "RESUME";
        case zv_event_opcode_resume_remove:
            return "RESUME_REMOVE";
        case zv_event_opcode_resume_wait_remove:
            return "RESUME_WAIT_REMOVE";
        case zv_event_opcode_remove:
            return "ACTION_REMOVE";
        default:
            assert(0);
            return "<UNKNOWN>";
    }
}

struct zv_loop_s * _zv_loop_create_select(const struct zv_loop_config_s * config);
struct zv_loop_s * _zv_loop_create_poll(const struct zv_loop_config_s * config);

zv_loop_t * zv_loop_create(const struct zv_loop_config_s * config)
{
#if defined(_WIN32)
    WORD version_requested = MAKEWORD(2, 2);
    WSADATA wsa_data;
    int rc = WSAStartup(version_requested, &wsa_data);
    if (rc != 0)
        return NULL;

    if ((HIBYTE(wsa_data.wVersion) != 2) && (LOBYTE(wsa_data.wVersion) != 2))
        return NULL;
#endif

    if (config == NULL)
        config = zv_loop_config_get_default();

    struct zv_loop_s * loop;

    switch (config->loop_type) {
        case zv_loop_type_select:
            loop = _zv_loop_create_select(config);
        break;
    #if !defined(__APPLE__)
        case zv_loop_type_poll:
            loop = _zv_loop_create_poll(config);
        break;
    #endif
        default:
            return NULL;
        break;
    }

    if (loop == NULL)
        return NULL;

    loop->head = NULL;
    loop->tail_n_flag = NULL;

    return loop;
}


int _zv_loop_init(
    struct zv_loop_s * loop,
    const struct zv_loop_config_s * config,
    enum zv_loop_type_e (*get_type)(struct zv_loop_s*),
    void (*run)(struct zv_loop_s*),
    void (*wakeup)(struct zv_loop_s*),
    void (*deinit)(struct zv_loop_s*)
) {
    // We will use thread pool with timer pool,
    // will be better to have at least 2 threads as the timer pool
    // requires one thread exclusively for waiting operations.
    size_t thread_pool_threads = config->thread_pool.threads;
    if (thread_pool_threads != 0)
    {
        if (thread_pool_threads < 2)
            thread_pool_threads = 2;
    }

    zv_thread_pool_t * thread_pool = zv_thread_pool_create(thread_pool_threads);
    if (thread_pool == NULL)
        return -1;

    pthread_key_t tls_iov;
    if (pthread_key_create(&tls_iov, &free) == 0)
    {
        const clockid_t * clockid = config->timer_pool.use_custom_clock
            ? &config->timer_pool.clockid
            : NULL;

        zv_timer_pool_t * timer_pool = zv_timer_pool_create(thread_pool, clockid);
        if (timer_pool)
        {
            size_t iov_max = config->channel.iov_max;
            if (iov_max == 0)
                iov_max = 512;
#if defined(_WIN32)
#elif defined(__linux__)
            size_t sys_iov_max = sysconf(_SC_IOV_MAX);
            if (iov_max > sys_iov_max)
                iov_max = sys_iov_max;
#elif defined(__APPLE__)
            if (iov_max > IOV_MAX)
                iov_max = IOV_MAX;
#endif
            loop->config = *config;
            loop->config.channel.iov_max = iov_max;
            loop->thread_pool = thread_pool;
            loop->timer_pool = timer_pool;
            loop->get_type = get_type;
            loop->run = run;
            loop->wakeup = wakeup;
            loop->deinit = deinit;
            loop->tls_iov = tls_iov;

            return 0;
        }
        pthread_key_delete(tls_iov);
    }
    zv_thread_pool_destroy(thread_pool);
    return -1;
}


enum zv_loop_type_e zv_loop_get_type(zv_loop_t * loop)
{
    return loop->get_type(loop);
}


zv_thread_pool_t * zv_loop_get_thread_pool(zv_loop_t * loop)
{
    return loop->thread_pool;
}


zv_timer_pool_t * zv_loop_get_timer_pool(zv_loop_t * loop)
{
    return loop->timer_pool;
}


void zv_loop_run(zv_loop_t * loop)
{
    loop->run(loop);
}


int _zv_loop_add_handler(zv_loop_t * loop, struct zv_event_handler_s * handler)
{
    assert(handler->next == NULL);
    assert((handler->opcode == zv_event_opcode_add) || (handler->opcode == zv_event_opcode_remove));
    struct zv_event_handler_s * tail_n_flag = atomic_load(&loop->tail_n_flag);
    for (;;)
    {
        const uintptr_t stop = (((uintptr_t)tail_n_flag) & 1);
        if (stop)
        {
            // Adding new acceptor or connector is not allowed
            // when loop is being stopped
            if ((handler->event_type == zv_event_type_accept) && (handler->opcode == zv_event_opcode_add))
                return -1;
            if ((handler->event_type == zv_event_type_connect) && (handler->opcode == zv_event_opcode_add))
                return -1;
        }

        struct zv_event_handler_s * new_tail_n_flag = (struct zv_event_handler_s*) (((uintptr_t)handler) | stop);
        if (atomic_compare_exchange_strong(&loop->tail_n_flag, &tail_n_flag, new_tail_n_flag))
        {
            struct zv_event_handler_s * tail = (struct zv_event_handler_s*) (((uintptr_t)tail_n_flag) - stop);
            if (tail)
                atomic_store(&tail->next, handler);
            else
            {
                atomic_store(&loop->head, handler);
                loop->wakeup(loop);
            }
            return 0;
        }
    }
}


void _zv_loop_resume_handler(zv_loop_t * loop, struct zv_event_handler_s * handler)
{
    assert(handler->opcode != zv_event_opcode_add);
    assert((handler->event_type == zv_event_type_accept) || (handler->event_type == zv_event_type_read));
    struct zv_event_handler_s * tail_n_flag = atomic_load(&loop->tail_n_flag);
    for (;;)
    {
        const uintptr_t stop = (((uintptr_t)tail_n_flag) & 1);
        struct zv_event_handler_s * new_tail_n_flag = (struct zv_event_handler_s*) (((uintptr_t)handler) | stop);
        if (atomic_compare_exchange_strong(&loop->tail_n_flag, &tail_n_flag, new_tail_n_flag))
        {
            struct zv_event_handler_s * tail = (struct zv_event_handler_s*) (((uintptr_t)tail_n_flag) - stop);
            if (tail == NULL)
                atomic_store(&loop->head, handler);
            else
                atomic_store(&tail->next, handler);
            break;
        }
    }
}


struct zv_event_handler_s * _zv_loop_pop(
    struct zv_loop_s * loop,
    struct zv_event_handler_s * event_handler
) {
    struct zv_event_handler_s * next = atomic_load(&event_handler->next);
    if (next == NULL)
    {
        atomic_store(&loop->head, NULL);
        struct zv_event_handler_s * tail_n_flag = atomic_load(&loop->tail_n_flag);
        uintptr_t stop = (((uintptr_t)tail_n_flag) & 1);
        struct zv_event_handler_s * tail = (struct zv_event_handler_s*) (((uintptr_t)tail_n_flag) - stop);
        if (event_handler == tail)
        {
            struct zv_event_handler_s * new_tail_n_flag = (struct zv_event_handler_s*) stop;
            if (atomic_compare_exchange_strong(&loop->tail_n_flag, &tail_n_flag, new_tail_n_flag))
                return NULL;

            // Stop flag could be set after we read 'tail_n_flag'
            stop = (((uintptr_t)tail_n_flag) & 1);
            tail = (struct zv_event_handler_s*)(((uintptr_t)tail_n_flag) - stop);
            if (tail == event_handler)
            {
                new_tail_n_flag = (struct zv_event_handler_s*) (uintptr_t) 1;
                if (atomic_compare_exchange_strong(&loop->tail_n_flag, &tail_n_flag, new_tail_n_flag))
                    return NULL;
            }
        }
        while ((next = atomic_load(&event_handler->next)) == NULL)
            _zv_atomic_wait();
    }
    event_handler->next = NULL;
    return next;
}


void zv_loop_stop(zv_loop_t * loop)
{
    ZV_LOG_DEBUG(_zv_loop_log, "0x%" PRIxPTR ": zv_loop_stop", loop);

    struct zv_event_handler_s * tail_n_flag = atomic_load(&loop->tail_n_flag);
    for (;;)
    {
        if (((uintptr_t)tail_n_flag) & 1)
            break;

        struct zv_event_handler_s * new_tail_n_flag = (struct zv_event_handler_s*) (((uintptr_t)tail_n_flag) | 1);
        if (atomic_compare_exchange_strong(&loop->tail_n_flag, &tail_n_flag, new_tail_n_flag))
        {
            if (!tail_n_flag)
                loop->wakeup(loop);
            break;
        }
    }
}


void zv_loop_destroy(zv_loop_t * loop)
{
    loop->deinit(loop);
    zv_timer_pool_destroy(loop->timer_pool);
    pthread_key_delete(loop->tls_iov);
    zv_thread_pool_destroy(loop->thread_pool);
    free(loop);
}


void zv_loop_set_log_level(enum zv_log_level level)
{
    _zv_loop_log.level = level;
}
