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

#include "channel_in_queue.h"
#include "zv/buffer.h"
#include "loop_int.h"
#include <assert.h>

#if defined(_WIN32)
#include <limits.h>
#include <WinSock2.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <stdint.h>
#include <sys/uio.h>
#endif

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

#if defined(_LP64) || defined(__LP64__) || defined(_M_X64)
#define STATE_SIZE_MASK      0x0FFFFFFFFFFFFFFFULL
#define STATE_FLAG_CLOSE     0x1000000000000000ULL
#define STATE_FLAG_SUSPENDED 0x2000000000000000ULL
#define STATE_FLAG_STOP      0x4000000000000000ULL
#else
#define STATE_SIZE_MASK      0x0FFFFFFFUL
#define STATE_FLAG_CLOSE     0x10000000UL
#define STATE_FLAG_SUSPENDED 0x20000000UL
#define STATE_FLAG_STOP      0x40000000UL
#endif

void zv_channel_in_queue_init(
    struct zv_channel_in_queue_s * in_queue,
    const struct zv_channel_in_queue_config_s * config,
    zv_buffer_cache_t * buffer_cache
) {
    in_queue->config = *config;
    in_queue->buffer_cache = buffer_cache;
    in_queue->head = zv_buffer_cache_get(buffer_cache);
    in_queue->rd_offs = 0;
    in_queue->tail = in_queue->head;
    in_queue->wr_offs = 0;
    in_queue->state = 0;
}


void zv_channel_in_queue_deinit(struct zv_channel_in_queue_s * in_queue)
{
    zv_buffer_cache_t * buffer_cache = in_queue->buffer_cache;
    // Sanity check: verify that the input queue is in the correct state during deinitialization.
    // The correct state means the input queue is empty, which can be represented by either:
    //     - (head == tail) && (wr_offs == rd_offs)
    //     - (head->next == tail) && (wr_offs == buffer_size) && (rd_offs == 0)
    // These specific conditions result from the lock-free implementation.
    assert(
        ((in_queue->head == in_queue->tail) && (in_queue->wr_offs == in_queue->rd_offs))
        || ((in_queue->head->next == in_queue->tail)
             && (in_queue->wr_offs == 0)
             && (in_queue->rd_offs == zv_buffer_cache_get_buffer_size(buffer_cache))
           )
    );
    zv_buffer_cache_put(buffer_cache, in_queue->head);
}


static inline struct iovec make_iovec(char * ptr, size_t len)
{
#if defined(_WIN32)
    WSABUF ret = { .buf = ptr, .len = (ULONG) len };
#else
    struct iovec ret = { .iov_base = ptr, .iov_len = len };
#endif
    return ret;
}


void zv_channel_in_queue_get_wr(
    struct zv_channel_in_queue_s * in_queue,
    struct iovec * iov
) {
    const size_t buffer_size = zv_buffer_cache_get_buffer_size(in_queue->buffer_cache);
    size_t space = (buffer_size - in_queue->wr_offs);
    if (space == 0)
    {
        struct zv_buffer_s * buffer = in_queue->tail->next;
        if (!buffer)
        {
            buffer = zv_buffer_cache_get(in_queue->buffer_cache);
            in_queue->tail->next = buffer;
        }
        in_queue->tail = buffer;
        in_queue->wr_offs = 0;
        space = buffer_size;
    }

    struct zv_buffer_s * buffer = in_queue->tail;
    iov[0] = make_iovec(((char*)(buffer + 1)) + in_queue->wr_offs, space);

    buffer = in_queue->tail->next;
    if (!buffer)
    {
        buffer = zv_buffer_cache_get(in_queue->buffer_cache);
        in_queue->tail->next = buffer;
    }
    iov[1] = make_iovec((char*)(buffer + 1), buffer_size);
}


size_t zv_channel_in_queue_move_wr(
    struct zv_channel_in_queue_s * in_queue,
    size_t bytes,
    int * psuspend
) {
    const size_t buffer_size = zv_buffer_cache_get_buffer_size(in_queue->buffer_cache);
    size_t space = (buffer_size - in_queue->wr_offs);
    if (bytes > space)
    {
        in_queue->tail = in_queue->tail->next;
        in_queue->wr_offs = (bytes - space);
    }
    else
        in_queue->wr_offs += bytes;

    size_t state = atomic_load(&in_queue->state);
    for (;;)
    {
        assert((state & STATE_FLAG_SUSPENDED) == 0);
        size_t new_state = (state + bytes);
        size_t new_size = (new_state & STATE_SIZE_MASK);
        int suspend;
        if (new_size > in_queue->config.high_watermark)
        {
            new_state |= STATE_FLAG_SUSPENDED;
            suspend = 1;
        }
        else
            suspend = 0;

        if (atomic_compare_exchange_strong(&in_queue->state, &state, new_state))
        {
            *psuspend = suspend;
            return new_size;
        }
    }
}


static const void * _zv_channel_in_queue_get_data(
    struct zv_channel_in_queue_s * in_queue,
    size_t * bytes_ready
) {
    const size_t buffer_size = zv_buffer_cache_get_buffer_size(in_queue->buffer_cache);
    size_t rd_offs = in_queue->rd_offs;
    struct zv_buffer_s * head = in_queue->head;
    size_t space = (buffer_size - rd_offs);
    if (space == 0)
    {
        struct zv_buffer_s * next = head->next;
        head->next = NULL;
        zv_buffer_cache_put(in_queue->buffer_cache, head);
        in_queue->head = next;
        in_queue->rd_offs = 0;
        space = buffer_size;
        rd_offs = 0;
        head = next;
    }

    if (space < *bytes_ready)
        *bytes_ready = space;

    return ((char*)(head + 1)) + rd_offs;
}


size_t zv_channel_in_queue_process_data(
    struct zv_channel_in_queue_s * in_queue,
    size_t bytes_ready,
    void (*input_handler)(zv_channel_t*, const void*, size_t),
    zv_channel_t * channel,
    int * presume_action
) {
    size_t bytes_remaining = bytes_ready;
    do
    {
        size_t size = bytes_remaining;
        const void * data = _zv_channel_in_queue_get_data(in_queue, &size);
        input_handler(channel, data, size);
        in_queue->rd_offs += size;
        assert(in_queue->rd_offs <= zv_buffer_cache_get_buffer_size(in_queue->buffer_cache));
        bytes_remaining -= size;
    }
    while (bytes_remaining > 0);

    size_t state = atomic_load(&in_queue->state);
    for (;;)
    {
        const size_t size = (state & STATE_SIZE_MASK);
        assert(size >= bytes_ready);
        size_t new_state = (state - bytes_ready);
        size_t new_size = (new_state & STATE_SIZE_MASK);
        int resume_action;
        if ((state & STATE_FLAG_SUSPENDED) && (new_size < in_queue->config.low_watermark))
        {
            new_state ^= STATE_FLAG_SUSPENDED;
            resume_action = 0x100;
            if (new_state & STATE_FLAG_STOP)
                resume_action |= zv_event_opcode_resume_wait_remove;
            else
                resume_action |= zv_event_opcode_resume;
        }
        else
            resume_action = 0;

        size_t old_state = state;
        if (atomic_compare_exchange_strong(&in_queue->state, &state, new_state))
        {
            *presume_action = resume_action;
            if (new_size > 0)
            {
                if (new_state & STATE_FLAG_SUSPENDED)
                {
                    // Return less size to trigger channel resume sooner.
                    new_size -= (in_queue->config.low_watermark - 1);
                }
                return new_size;
            }
            else if (state & STATE_FLAG_CLOSE)
                return SIZE_MAX;
            else
                return 0;
        }

        if (old_state & STATE_FLAG_SUSPENDED)
        {
            // sanity check: only STATE_FLAG_STATE can be changed outside
            assert((old_state & STATE_SIZE_MASK) == (state & STATE_SIZE_MASK));
            assert((old_state & STATE_FLAG_CLOSE) == (state & STATE_FLAG_CLOSE));
            assert((old_state & STATE_FLAG_STOP) == 0);
            assert((state & STATE_FLAG_STOP) != 0);
        }
    }
}


int zv_channel_in_queue_close(
    struct zv_channel_in_queue_s * in_queue,
    int * is_stopped
) {
    size_t state = atomic_load(&in_queue->state);
    for (;;)
    {
        assert((state & STATE_FLAG_CLOSE) == 0);
        size_t new_state = (state | STATE_FLAG_CLOSE);
        if (atomic_compare_exchange_strong(&in_queue->state, &state, new_state))
        {
            *is_stopped = ((state & STATE_FLAG_STOP) != 0);
            return ((state & STATE_SIZE_MASK) == 0);
        }
    }
}


int zv_channel_in_queue_stop(
    struct zv_channel_in_queue_s * in_queue
) {
    size_t state = atomic_load(&in_queue->state);
    for (;;)
    {
        if (state & (STATE_FLAG_CLOSE|STATE_FLAG_STOP))
            return -1;

        size_t new_state = (state | STATE_FLAG_STOP);
        if (atomic_compare_exchange_strong(&in_queue->state, &state, new_state))
            return 0;
    }
}
