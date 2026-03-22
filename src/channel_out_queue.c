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

#include "channel_out_queue.h"
#include "zv/buffer.h"
#include "misc.h"

#if defined(_WIN32)
#include <limits.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <stdint.h>
#include <string.h>
#endif

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

#if defined(_LP64) || defined(__LP64__) || defined(_M_X64)
#define STATE_SIZE_MASK       0x0FFFFFFFFFFFFFFFULL
#define STATE_FLAG_CLOSE      0x1000000000000000ULL
#else
#define STATE_SIZE_MASK   0x0FFFFFFFUL
#define STATE_FLAG_CLOSE  0x10000000UL
#endif


void zv_channel_out_queue_init(
    struct zv_channel_out_queue_s * out_queue,
    zv_buffer_cache_t * buffer_cache
) {
    out_queue->buffer_cache = buffer_cache;
    out_queue->head = zv_buffer_cache_get(buffer_cache);
    out_queue->tail = out_queue->head;
    out_queue->rd_offs = 0;
    out_queue->add_state = 0;
    out_queue->state = 0;
}


void zv_channel_out_queue_deinit(
    struct zv_channel_out_queue_s * out_queue
) {
    zv_buffer_cache_put(out_queue->buffer_cache, out_queue->head);
}


static size_t _zv_channel_out_queue_add_data(
    struct zv_channel_out_queue_s * out_queue,
    const void * data,
    size_t size
) {
    const size_t buffer_size = zv_buffer_cache_get_buffer_size(out_queue->buffer_cache);
    const size_t ONE = 1;
#if defined(_LP64) || defined(__LP64__) || defined(_M_X64)
    const int OFFS_BITS = 26;
    const int WRITERS_BITS = 6;
#else
    const int OFFS_BITS = 14;
    const int WRITERS_BITS = 2;
#endif
    const size_t OFFS_MASK = ((ONE << OFFS_BITS) - 1);
    const size_t WRITER = (ONE << (OFFS_BITS * 2));
    const size_t WRITERS_MASK = (((ONE << WRITERS_BITS) - 1) << (OFFS_BITS * 2));
    const size_t TAIL_LOCK = (ONE << (OFFS_BITS * 2 + WRITERS_BITS));

    size_t state = atomic_load(&out_queue->add_state);
    for (;;)
    {
        if (state & TAIL_LOCK)
        {
            _zv_atomic_wait();
            state = atomic_load(&out_queue->add_state);
            continue;
        }

        size_t offs = (state & OFFS_MASK);
        size_t space = (buffer_size - offs);
        if (size > space)
        {
            if (state & WRITERS_MASK)
            {
                _zv_atomic_wait();
                state = atomic_load(&out_queue->add_state);
                continue;
            }

            size_t new_state = (state | TAIL_LOCK);
            if (!atomic_compare_exchange_strong(&out_queue->add_state, &state, new_state))
                continue;

            if (space > 0)
            {
                void* dst = ((char*)(out_queue->tail + 1)) + offs;
                memcpy(dst, data, space);
                data = ((const char*)data) + space;
            }

            size_t bytes = (size - space);
            struct zv_buffer_s * buffer = zv_buffer_cache_get_for(out_queue->buffer_cache, bytes);
            out_queue->tail->next = buffer;
            for (;;)
            {
                if (bytes <= buffer_size)
                {
                    memcpy((buffer + 1), data, bytes);
                    break;
                }

                memcpy((buffer + 1), data, buffer_size);
                data = ((const char*)data) + buffer_size;
                bytes -= buffer_size;
                buffer = buffer->next;
            }

            out_queue->tail = buffer;
            atomic_store(&out_queue->add_state, bytes);
            return size;
        }

        size_t writers = (state & WRITERS_MASK);
        if (writers == WRITERS_MASK)
        {
            _zv_atomic_wait();
            state = atomic_load(&out_queue->add_state);
            continue;
        }

        size_t new_state = state;
        if (writers == 0)
        {
            size_t start_offs = (offs << OFFS_BITS);
            new_state |= start_offs;
        }
        new_state += size;
        new_state += WRITER;

        if (!atomic_compare_exchange_strong(&out_queue->add_state, &state, new_state))
            continue;

        void * dst = ((char*)(out_queue->tail + 1)) + offs;
        memcpy(dst, data, size);

        state = new_state;
        for (;;)
        {
            new_state = (state - WRITER);
            size_t start_offs = ((state >> OFFS_BITS) & OFFS_MASK);
            if ((new_state & WRITERS_MASK) == 0)
            {
                new_state &= ~(OFFS_MASK << OFFS_BITS);
                if (atomic_compare_exchange_strong(&out_queue->add_state, &state, new_state))
                    return ((new_state & OFFS_MASK) - start_offs);
            }
            else if (offs == start_offs)
            {
                new_state &= ~(OFFS_MASK << OFFS_BITS);
                new_state |= ((offs + size) << OFFS_BITS);
                if (atomic_compare_exchange_strong(&out_queue->add_state, &state, new_state))
                    return size;
            }
            else
            {
                if (atomic_compare_exchange_strong(&out_queue->add_state, &state, new_state))
                    return 0;
            }
        }
    }
}

int zv_channel_out_queue_add_data(
    struct zv_channel_out_queue_s * out_queue,
    const void * data,
    size_t size
) {
    size_t state = atomic_load(&out_queue->state);
    if (state & STATE_FLAG_CLOSE)
        return -1;

    size_t bytes_ready = _zv_channel_out_queue_add_data(out_queue, data, size);
    if (bytes_ready == 0)
    {
        // can happen if current thread finished write
        // sooner than other thread started writting data in the area with lower address
        return 0;
    }

    for (;;)
    {
        size_t new_state = (state + bytes_ready);
        if (atomic_compare_exchange_strong(&out_queue->state, &state, new_state))
        {
            if ((state & STATE_SIZE_MASK) == 0)
                return 1;
            else
                return 0;
        }

        if (state & STATE_FLAG_CLOSE)
            return -1;
    }
}

#if defined(_WIN32)
static inline WSABUF make_iovec(char* ptr, size_t len)
{
    WSABUF ret = { .buf = ptr, .len = (ULONG)len };
    return ret;
}
#else
static inline struct iovec make_iovec(char* ptr, size_t len)
{
    struct iovec ret = { .iov_base = ptr, .iov_len = len };
    return ret;
}
#endif


int zv_channel_out_queue_get_data(
    struct zv_channel_out_queue_s * out_queue,
#if defined(_WIN32)
    WSABUF * iov,
#else
    struct iovec * iov,
#endif
    int iovc_max
) {
    const size_t buffer_size = zv_buffer_cache_get_buffer_size(out_queue->buffer_cache);
    size_t state = atomic_load(&out_queue->state);
    size_t bytes_ready = (state & STATE_SIZE_MASK);

    struct zv_buffer_s * buffer = out_queue->head;
    size_t rd_offs = out_queue->rd_offs;
    size_t cc = (buffer_size - rd_offs);
    if (cc == 0)
    {
        buffer = buffer->next;
        cc = buffer_size;
        rd_offs = 0;
    }

    char * ptr = (((char*)(buffer + 1)) + rd_offs);
    if (bytes_ready <= cc)
    {
        iov[0] = make_iovec(ptr, bytes_ready);
        return 1;
    }
    else
    {
        iov[0] = make_iovec(ptr, cc);
        bytes_ready -= cc;
        buffer = buffer->next;

        int iovc = 1;
        while (iovc < iovc_max)
        {
            iovc++;
            iov++;

            ptr = (char*)(buffer + 1);
            if (bytes_ready <= buffer_size)
            {
                *iov = make_iovec(ptr, bytes_ready);
                break;
            }

            *iov = make_iovec(ptr, buffer_size);
            buffer = buffer->next;
            if (buffer == NULL)
                break;

            bytes_ready -= buffer_size;
        }

        return iovc;
    }
}


size_t zv_channel_out_queue_remove_data(
    struct zv_channel_out_queue_s * out_queue,
    size_t bytes
) {
    const size_t buffer_size = zv_buffer_cache_get_buffer_size(out_queue->buffer_cache);
    size_t cc = (buffer_size - out_queue->rd_offs);
    if (bytes <= cc)
        out_queue->rd_offs += bytes;
    else
    {
        struct zv_buffer_s * head = out_queue->head;
        struct zv_buffer_s * tail = head;
        struct zv_buffer_s * buffer = head;

        size_t bytes_remaining = (bytes - cc);
        buffer = buffer->next;

        while (bytes_remaining > buffer_size)
        {
            tail = buffer;
            buffer = buffer->next;
            bytes_remaining -= buffer_size;
        }

        out_queue->head = buffer;
        out_queue->rd_offs = bytes_remaining;

        tail->next = NULL;
        zv_buffer_cache_put(out_queue->buffer_cache, head);
    }

    size_t state = atomic_fetch_sub(&out_queue->state, bytes) - bytes;
    size_t bytes_remaining = (state & STATE_SIZE_MASK);
    if (bytes_remaining > 0)
        return bytes_remaining;
    else if (state & STATE_FLAG_CLOSE)
        return SIZE_MAX;
    else
        return 0;
}


int zv_channel_out_queue_close(
    struct zv_channel_out_queue_s * out_queue,
    int * is_empty
) {
    size_t state = atomic_load(&out_queue->state);
    for (;;)
    {
        const size_t bytes_remaining = (state & STATE_SIZE_MASK);
        if (state & STATE_FLAG_CLOSE)
        {
            *is_empty = (bytes_remaining == 0);
            return -1;
        }

        size_t new_state = (state | STATE_FLAG_CLOSE);
        if (atomic_compare_exchange_strong(&out_queue->state, &state, new_state))
        {
            *is_empty = (bytes_remaining == 0);
            return 0;
        }
    }
}
