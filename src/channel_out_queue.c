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
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <limits.h>
#include <WinSock2.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/uio.h>
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

#if defined(_WIN32)
#define iovec _WSABUF
static inline size_t iovec_get_len(const struct iovec * iov) { return iov->len; }
static inline void * iovec_get_base(const struct iovec * iov) { return iov->buf; }
#else
static inline size_t iovec_get_len(const struct iovec* iov) { return iov->iov_len; }
static inline void * iovec_get_base(const struct iovec* iov) { return iov->iov_base; }
#endif

static inline struct iovec make_iovec(void * ptr, size_t len)
{
#if defined(_WIN32)
    WSABUF ret = { .buf = ptr, .len = (ULONG)len };
#else
    struct iovec ret = { .iov_base = ptr, .iov_len = len };
#endif
    return ret;
}

static inline size_t uleb128_encode(void * buffer, size_t value)
{
    unsigned char * ptr = (unsigned char*) buffer;
    for (;;)
    {
        unsigned char ch = (value & 0x7F);
        value >>= 7;
        if (value == 0)
        {
            *(ptr++) = ch;
            break;
        }
        *(ptr++) = (ch | 0x80);
    }
    return (ptr - (unsigned char*)buffer);
}


static inline size_t uleb128_size(size_t value)
{
    size_t size = 1;
    while (value > 0x7F)
    {
        value >>= 7;
        size++;
    }
    return size;
}


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
    const struct iovec * iov,
    int iovc
) {
    const size_t buffer_size = zv_buffer_cache_get_buffer_size(out_queue->buffer_cache);
    union state_u
    {
        struct state_s
        {
#if defined(_LP64) || defined(__LP64__) || defined(_M_X64)
            size_t offs : 23;
            size_t start_offs : 23;
            size_t blocks : 8;
            size_t writers : 6;
#else
            size_t offs : 13;
            size_t start_offs : 13;
            size_t blocks : 3;
            size_t writers : 2;
#endif
            size_t lock : 1;
        }
        as_struct;
        size_t as_uint;
    };

    size_t size = 0;
    for (int idx=0; idx<iovc; idx++)
        size += iovec_get_len(&iov[idx]);

    union state_u state = { .as_uint = atomic_load(&out_queue->add_state) };
    for (;;)
    {
        if (state.as_struct.lock)
        {
            _zv_atomic_wait();
            state.as_uint = atomic_load(&out_queue->add_state);
            continue;
        }

        size_t offs = state.as_struct.offs;
        size_t space = (buffer_size - offs);
        if (size > space)
        {
            if (state.as_struct.writers > 0)
            {
                _zv_atomic_wait();
                state.as_uint = atomic_load(&out_queue->add_state);
                continue;
            }

            union state_u new_state = state;
            new_state.as_struct.lock = 1;
            if (!atomic_compare_exchange_strong(&out_queue->add_state, &state.as_uint, new_state.as_uint))
                continue;

            const void * iov_base = iovec_get_base(&iov[0]);
            size_t iov_len = iovec_get_len(&iov[0]);

            if (space > 0)
            {
                void * dst = ((char*)(out_queue->tail + 1)) + offs;
                for (;;)
                {
                    if (iov_len > space)
                    {
                        memcpy(dst, iov_base, space);
                        iov_base = ((const char*)iov_base) + space;
                        iov_len -= space;
                        break;
                    }
                    else if (iov_len == space)
                    {
                        memcpy(dst, iov_base, space);
                        iov++;
                        iov_base = iovec_get_base(iov);
                        iov_len = iovec_get_len(iov);
                        iovc--;
                        break;
                    }
                    else // iov_len < space
                    {
                        memcpy(dst, iov_base, iov_len);
                        dst = ((char*)dst) + iov_len;
                        space -= iov_len;
                        iov++;
                        iov_base = iovec_get_base(iov);
                        iov_len = iovec_get_len(iov);
                        iovc--;
                    }
                }
            }

            size_t bytes = (size - space);
            struct zv_buffer_s * buffer = zv_buffer_cache_get_for(out_queue->buffer_cache, bytes);
            space = buffer_size;
            out_queue->tail->next = buffer;
            void * dst = (buffer + 1);
            for (;;)
            {
                if (iov_len > space)
                {
                    memcpy(dst, iov_base, space);
                    iov_base = ((const char*)iov_base) + space;
                    iov_len -= space;
                    buffer = buffer->next;
                    space = buffer_size;
                    dst = (buffer + 1);
                }
                else
                {
                    // iov_len <= space
                    memcpy(dst, iov_base, iov_len);
                    space -= iov_len;
                    if (--iovc == 0)
                        break;
                    if (space == 0)
                    {
                        buffer = buffer->next;
                        space = buffer_size;
                        dst = (buffer + 1);
                    }
                    else
                        dst = (((char*)dst) + iov_len);
                    iov++;
                    iov_base = iovec_get_base(iov);
                    iov_len = iovec_get_len(iov);
                }
            }

            out_queue->tail = buffer;
            new_state.as_uint = 0;
            new_state.as_struct.offs = (buffer_size - space);
            atomic_store(&out_queue->add_state, new_state.as_uint);
            return 1;
        }

        const size_t max_blocks = ((struct state_s) { .blocks = ~0 }).blocks;
        if (state.as_struct.blocks == max_blocks)
        {
            _zv_atomic_wait();
            state.as_uint = atomic_load(&out_queue->add_state);
            continue;
        }

        const size_t max_writers = ((struct state_s){.writers = ~0}).writers;
        size_t writers = state.as_struct.writers;
        if (writers == max_writers)
        {
            _zv_atomic_wait();
            state.as_uint = atomic_load(&out_queue->add_state);
            continue;
        }

        union state_u new_state = state;
        if (writers == 0)
            new_state.as_struct.start_offs = offs;

        new_state.as_struct.offs += size;
        new_state.as_struct.blocks++;
        new_state.as_struct.writers++;

        if (!atomic_compare_exchange_strong(&out_queue->add_state, &state.as_uint, new_state.as_uint))
            continue;

        void * dst = ((char*)(out_queue->tail + 1)) + offs;
        for (;;)
        {
            memcpy(dst, iovec_get_base(iov), iovec_get_len(iov));
            if (--iovc == 0)
                break;
            dst = ((char*)dst) + iovec_get_len(iov);
            iov++;
        }

        state = new_state;
        for (;;)
        {
            size_t start_offs = new_state.as_struct.start_offs;
            new_state.as_struct.writers--;
            if (new_state.as_struct.writers == 0)
            {
                new_state.as_struct.start_offs = 0;
                size_t blocks = new_state.as_struct.blocks;
                new_state.as_struct.blocks = 0;
                if (atomic_compare_exchange_strong(&out_queue->add_state, &state.as_uint, new_state.as_uint))
                    return blocks;
            }
            else if (offs == start_offs)
            {
                new_state.as_struct.start_offs = (offs + size);
                new_state.as_struct.blocks--;
                if (atomic_compare_exchange_strong(&out_queue->add_state, &state.as_uint, new_state.as_uint))
                    return 1;
            }
            else
            {
                if (atomic_compare_exchange_strong(&out_queue->add_state, &state.as_uint, new_state.as_uint))
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

    char header[11];
    header[0] = 0;
    size_t header_size = (1 + uleb128_encode(&header[1], size));

    struct iovec iov[2];
    iov[0] = make_iovec(header, header_size);
    iov[1] = make_iovec((void*)data, size);

    size_t blocks_ready = _zv_channel_out_queue_add_data(out_queue, iov, 2);
    if (blocks_ready == 0)
    {
        // can happen if current thread finished write
        // sooner than other thread started writting data in the area with lower offset
        return 0;
    }

    for (;;)
    {
        size_t new_state = (state + blocks_ready);
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


int zv_channel_out_queue_get_data(
    struct zv_channel_out_queue_s * out_queue,
    struct iovec * iov,
    int iov_count
) {
    const size_t buffer_size = zv_buffer_cache_get_buffer_size(out_queue->buffer_cache);
    size_t state = atomic_load(&out_queue->state);
    size_t blocks = (state & STATE_SIZE_MASK);

    struct zv_buffer_s * buffer = out_queue->head;
    size_t rd_offs = out_queue->rd_offs;
    if (rd_offs == buffer_size)
    {
        rd_offs = 0;
        buffer = buffer->next;
    }

    int iovc = 0;
    size_t cc = (buffer_size - rd_offs);
    unsigned char * ptr = (unsigned char*) (buffer + 1);
    ptr += rd_offs;

    for (;;)
    {
        unsigned char block_type = *(ptr++);
        cc--;
        if (block_type == 0)
        {
            size_t data_size = 0;
            int shift = 0;
            for (;;)
            {
                if (cc == 0)
                {
                    buffer = buffer->next;
                    ptr = (unsigned char*) (buffer + 1);
                    cc = buffer_size;
                }
                unsigned char ch = *(ptr++);
                data_size |= ((((size_t)ch) & 0x7F) << shift);
                cc--;
                if ((ch & 0x80) == 0)
                    break;
                shift += 7;
            }

            while (data_size > cc)
            {
                *iov = make_iovec(ptr, cc);
                if (++iovc == iov_count)
                    return iov_count;
                iov++;
                data_size -= cc;
                buffer = buffer->next;
                ptr = (unsigned char*) (buffer + 1);
                cc = buffer_size;
            }

            // data_size <= cc
            if (data_size > 0)
            {
                *iov = make_iovec(ptr, data_size);
                if (++iovc == iov_count)
                    return iov_count;
                iov++;
                ptr += data_size;
                cc -= data_size;
            }
        }
        else if (block_type == 1)
        {
            abort();
        }
        else
            abort();

        if (--blocks == 0)
            break;
    }

    return iovc;
}


struct buffer_cursor_s
{
    size_t buffer_size;
    struct zv_buffer_s * head;
    struct zv_buffer_s * tail;
    struct zv_buffer_s * buffer;
    unsigned char * ptr;
    size_t bytes_remaining;
};


static inline void buffer_cursor_init(
    struct buffer_cursor_s * bc,
    size_t buffer_size,
    struct zv_buffer_s * buffer,
    size_t offs
) {
    bc->buffer_size = buffer_size;
    bc->head = NULL;
    bc->tail = NULL;
    bc->buffer = buffer;
    bc->ptr = (unsigned char*) (buffer + 1);
    bc->ptr += offs;
    bc->bytes_remaining = (buffer_size - offs);
}


static inline size_t buffer_cursor_deinit(
    struct buffer_cursor_s * bc,
    struct zv_buffer_cache_s * buffer_cache
) {
    if (bc->head != NULL)
        zv_buffer_cache_put(buffer_cache, bc->head);
    return (bc->buffer_size - bc->bytes_remaining);
}


static inline unsigned char buffer_cursor_get(
    struct buffer_cursor_s * bc
) {
    if (bc->bytes_remaining == 0)
    {
        if (bc->head == NULL)
            bc->head = bc->buffer;
        else
            bc->tail->next = bc->buffer;
        struct zv_buffer_s * next = bc->buffer->next;
        bc->tail = bc->buffer;
        bc->tail->next = NULL;
        bc->buffer = next;
        bc->ptr = (unsigned char*) (next + 1);
        bc->bytes_remaining = bc->buffer_size;
    }
    unsigned char ret = *(bc->ptr);
    bc->ptr++;
    bc->bytes_remaining--;
    return ret;
}


static inline void buffer_cursor_put(
    struct buffer_cursor_s * bc,
    unsigned char ch
) {
    if (bc->bytes_remaining == bc->buffer_size)
    {
        struct zv_buffer_s * b = bc->head;
        bc->head = b->next;
        if (bc->head == NULL)
            bc->tail = NULL;
        b->next = bc->buffer;
        bc->buffer = b;
        bc->ptr = (unsigned char*) (b + 1);
        bc->ptr += bc->buffer_size;
        bc->bytes_remaining = 0;
    }
    bc->bytes_remaining++;
    bc->ptr--;
    *(bc->ptr) = ch;
}


static inline void buffer_cursor_move(
    struct buffer_cursor_s * bc,
    size_t count
) {
    while (count > bc->bytes_remaining)
    {
        count -= bc->bytes_remaining;
        if (bc->head == NULL)
            bc->head = bc->buffer;
        else
            bc->tail->next = bc->buffer;
        struct zv_buffer_s * next = bc->buffer->next;
        bc->tail = bc->buffer;
        bc->tail->next = NULL;
        bc->buffer = next;
        bc->ptr = (unsigned char*) (next + 1);
        bc->bytes_remaining = bc->buffer_size;
    }
    bc->ptr += count;
    bc->bytes_remaining -= count;
}


size_t zv_channel_out_queue_remove(
    struct zv_channel_out_queue_s * out_queue,
    size_t bytes
) {
    struct buffer_cursor_s bc;
    buffer_cursor_init(
        &bc,
        zv_buffer_cache_get_buffer_size(out_queue->buffer_cache),
        out_queue->head,
        out_queue->rd_offs
    );

    size_t blocks = 0;
    do
    {
        unsigned char block_type = buffer_cursor_get(&bc);
        if (block_type == 0)
        {
            size_t block_size = 0;
            for (int shift=0;; shift+=7)
            {
                unsigned char ch = buffer_cursor_get(&bc);
                block_size |= ((((size_t)ch) & 0x7F) << shift);
                if ((ch & 0x80) == 0)
                    break;
            }

            if (bytes < block_size)
            {
                block_size -= bytes;
                buffer_cursor_move(&bc, bytes);
                // encode block type with the remaining size
                size_t shift = 7 * (uleb128_size(block_size) - 1);
                unsigned char cont_flag = 0;
                for (;;)
                {
                    unsigned char ch = ((block_size >> shift) & 0x7F) | cont_flag;
                    buffer_cursor_put(&bc, ch);
                    if (shift == 0)
                        break;
                    cont_flag = 0x80;
                    shift -= 7;
                }
                buffer_cursor_put(&bc, 0);
                break;
            }
            else
            {
                blocks++;
                bytes -= block_size;
                buffer_cursor_move(&bc, block_size);
            }
        }
        else if (block_type == 1)
        {
            abort();
        }
        else
            abort();
    }
    while (bytes > 0);

    out_queue->head = bc.buffer;
    out_queue->rd_offs = buffer_cursor_deinit(&bc, out_queue->buffer_cache);

    if (blocks == 0)
        return 1;

    size_t state = atomic_fetch_sub(&out_queue->state, blocks) - blocks;
    blocks = (state & STATE_SIZE_MASK);
    if (blocks > 0)
        return blocks;
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
        const size_t blocks_remaining = (state & STATE_SIZE_MASK);
        if (state & STATE_FLAG_CLOSE)
        {
            *is_empty = (blocks_remaining == 0);
            return -1;
        }

        size_t new_state = (state | STATE_FLAG_CLOSE);
        if (atomic_compare_exchange_strong(&out_queue->state, &state, new_state))
        {
            *is_empty = (blocks_remaining == 0);
            return 0;
        }
    }
}
