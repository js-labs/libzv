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

#if !defined(ZV_LOOP_INT_H)
#define ZV_LOOP_INT_H

#include "zv/config.h"
#include "zv/os_socket.h"
#include "zv/os_thread.h"
#include "zv/thread_pool.h"
#include "zv/timer_pool.h"

#if defined(__INTELLISENSE__)
#define _Atomic
#endif

typedef enum
{
    zv_event_type_accept = 0,
    zv_event_type_read,
    zv_event_type_connect,
    zv_event_type_write
}
zv_event_type_t;

const char * zv_event_type_get_name(zv_event_type_t event_type);

typedef enum
{
    zv_event_opcode_add = 0,
    zv_event_opcode_resume,
    zv_event_opcode_resume_remove,
    zv_event_opcode_resume_wait_remove,
    zv_event_opcode_remove
}
zv_event_opcode_t;

const char * zv_event_opcode_get_name(zv_event_opcode_t event_opcode);

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

#define _ZV_HAS_RDTSC

static inline uint64_t _zv_rdtsc(void)
{
#if defined(_MSC_VER)
    return __rdtsc();
#else
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#endif
}

#elif defined(__APPLE__)

#include <mach/mach_time.h>
#define _ZV_HAS_RDTSC

static inline uint64_t _zv_rdtsc(void)
{
    //return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
    return mach_absolute_time();
}

#endif


struct zv_event_handler_s
{
    struct zv_event_handler_s * _Atomic next;
    zv_socket_t sockfd;
    // Store event type and event action in char to save some memory
    char /*zv_event_type_t*/ event_type;
    char /*zv_event_opcode_t*/ opcode;
    struct zv_thread_pool_task_s task;
};


static inline void _zv_event_handler_init(
    struct zv_event_handler_s * handler,
    zv_socket_t sockfd,
    zv_event_type_t event_type,
    zv_event_opcode_t opcode,
    void (*task_func)(struct zv_thread_pool_task_s*)
) {
    handler->next = NULL;
    handler->sockfd = sockfd;
    handler->event_type = event_type;
    handler->opcode = opcode;
    zv_thread_pool_task_init(&handler->task, task_func);
}


#if defined(_ZV_HAS_RDTSC)

// moving average
struct zv_mavg_s
{
    uint64_t value;
    size_t count;
};

static inline void _zv_mavg_init(struct zv_mavg_s * mavg)
{
    mavg->value = 0;
    mavg->count = 0;
}

static inline void _zv_mavg_append(struct zv_mavg_s * mavg, uint64_t value)
{
    size_t count = mavg->count;
    uint64_t cc = ((mavg->value * count) + value);
    if (count < 15)
    {
        count++;
        mavg->value = (cc / count);
        mavg->count = count;
    }
    else
        mavg->value = (cc >> 4);
}

struct zv_channel_read_stats_s
{
    uint64_t dispatch_tsc;
    struct zv_mavg_s dispatch;
    struct zv_mavg_s read;
    size_t total_reads;
    size_t async_reads;
};

static inline void _zv_channel_read_stats_init(
    struct zv_channel_read_stats_s * read_stats
) {
    read_stats->dispatch_tsc = 0;
    _zv_mavg_init(&read_stats->dispatch);
    _zv_mavg_init(&read_stats->read);
    read_stats->total_reads = 0;
    read_stats->async_reads = 0;
}

void _zv_channel_read(
    struct zv_event_handler_s * read_handler,
    struct zv_thread_pool_s * thread_pool
);

#endif /* !defined(_ZV_HAS_RDTSC) */

struct zv_loop_s
{
    struct zv_loop_config_s config;
    zv_thread_pool_t * thread_pool;
    zv_timer_pool_t * timer_pool;

    enum zv_loop_type_e (*get_type)(struct zv_loop_s * loop);
    void (*run)(struct zv_loop_s * loop);
    void (*wakeup)(struct zv_loop_s * loop);
    void (*deinit)(struct zv_loop_s * loop);

    pthread_key_t tls_iov;
    struct zv_event_handler_s * _Atomic head;
    char _padding1[64 - sizeof(void*)];
    struct zv_event_handler_s * _Atomic tail_n_flag;
};

int _zv_loop_init(
    struct zv_loop_s * loop,
    const struct zv_loop_config_s * config,
    enum zv_loop_type_e (*get_type)(struct zv_loop_s*),
    void (*run)(struct zv_loop_s*),
    void (*wakeup)(struct zv_loop_s*),
    void (*destroy)(struct zv_loop_s*)
);

int _zv_loop_add_handler(struct zv_loop_s * loop, struct zv_event_handler_s * handler);
void _zv_loop_resume_handler(struct zv_loop_s * loop, struct zv_event_handler_s * handler);

struct zv_event_handler_s * _zv_loop_pop(
    struct zv_loop_s * loop,
    struct zv_event_handler_s * event_handler
);

#endif /* ZV_LOOP_INT_H */
