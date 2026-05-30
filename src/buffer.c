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

#include "zv/buffer.h"
#include "zv/os_thread.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

struct zv_buffer_cache_s
{
    _Atomic size_t retain_count;
    size_t buffer_size;
    size_t max_size;
    pthread_mutex_t lock;
    size_t size;
    struct zv_buffer_s * buffer;
    size_t in_use;

    struct
    {
        size_t gets;
        size_t gets_alloc;
        size_t puts;
        size_t puts_free;
    }
    stats;
};


zv_buffer_cache_t * zv_buffer_cache_create(
    size_t buffer_size,
    size_t max_size,
    size_t init_size
) {
    if (init_size > max_size)
        init_size = max_size;

    struct zv_buffer_cache_s * buffer_cache = malloc(sizeof(struct zv_buffer_cache_s));
    if (buffer_cache == NULL)
        return NULL;

    buffer_cache->retain_count = 1;
    buffer_cache->buffer_size = buffer_size;
    buffer_cache->max_size = max_size;
    pthread_mutex_init(&buffer_cache->lock, NULL);
    buffer_cache->size = init_size;
    buffer_cache->buffer = NULL;
    buffer_cache->in_use = 0;

    buffer_cache->stats.gets = 0;
    buffer_cache->stats.gets_alloc = 0;
    buffer_cache->stats.puts = 0;
    buffer_cache->stats.puts_free = 0;

    for (size_t c=init_size; c>0; c--)
    {
        struct zv_buffer_s * buffer = malloc(sizeof(struct zv_buffer_s) + buffer_size);
        if (buffer == NULL)
            break;
        buffer->next = buffer_cache->buffer;
        buffer_cache->buffer = buffer;
    }

    return buffer_cache;
}


void zv_buffer_cache_retain(zv_buffer_cache_t * buffer_cache)
{
    atomic_fetch_add(&buffer_cache->retain_count, 1);
}


void zv_buffer_cache_release(zv_buffer_cache_t * buffer_cache)
{
    size_t retain_count = atomic_fetch_sub(&buffer_cache->retain_count, 1) - 1;
    if (retain_count == 0)
    {
        struct zv_buffer_s * buffer = buffer_cache->buffer;
        while (buffer != NULL)
        {
            struct zv_buffer_s * next = buffer->next;
            free(buffer);
            buffer = next;
        }
        pthread_mutex_destroy(&buffer_cache->lock);
        assert(buffer_cache->in_use == 0);
        /*
        printf(
            "buffer_cache[0x%p] stats: gets=%zu gets_alloc=%zu puts=%zu puts_free=%zu\n",
            buffer_cache,
            buffer_cache->stats.gets,
            buffer_cache->stats.gets_alloc,
            buffer_cache->stats.puts,
            buffer_cache->stats.puts_free
        );
        */
        free(buffer_cache);
    }
}


size_t zv_buffer_cache_get_buffer_size(zv_buffer_cache_t * buffer_cache)
{
    return buffer_cache->buffer_size;
}


struct zv_buffer_s * zv_buffer_cache_get(zv_buffer_cache_t * buffer_cache)
{
    pthread_mutex_lock(&buffer_cache->lock);
    buffer_cache->in_use++;
    buffer_cache->stats.gets++;
    if (buffer_cache->buffer)
    {
        struct zv_buffer_s * buffer = buffer_cache->buffer;
        buffer_cache->buffer = buffer->next;
        buffer_cache->size--;
        pthread_mutex_unlock(&buffer_cache->lock);
        buffer->next = NULL;
        return buffer;
    }
    else
    {
        buffer_cache->stats.gets_alloc++;
        pthread_mutex_unlock(&buffer_cache->lock);
        struct zv_buffer_s * buffer = malloc(sizeof(struct zv_buffer_s) + buffer_cache->buffer_size);
        if (buffer)
        {
            buffer->next = NULL;
            return buffer;
        }
        else
            return NULL;
    }
}


struct zv_buffer_s * zv_buffer_cache_get_for(zv_buffer_cache_t * buffer_cache, size_t size)
{
    const size_t buffer_size = buffer_cache->buffer_size;

    pthread_mutex_lock(&buffer_cache->lock);

    struct zv_buffer_s * head = buffer_cache->buffer;
    struct zv_buffer_s * tail = head;

    size_t count = ((size + buffer_size - 1) / buffer_size);
    buffer_cache->in_use += count;
    buffer_cache->stats.gets++;

    if (head != NULL)
    {
        for (;;)
        {
            buffer_cache->size--;
            if (size <= buffer_size)
            {
                buffer_cache->buffer = tail->next;
                pthread_mutex_unlock(&buffer_cache->lock);
                tail->next = NULL;
                return head;
            }

            size -= buffer_size;
            if (tail->next == NULL)
            {
                buffer_cache->buffer = NULL;
                break;
            }
            tail = tail->next;
        }
    }

    buffer_cache->stats.gets_alloc += ((size + buffer_size - 1) / buffer_size);

    pthread_mutex_unlock(&buffer_cache->lock);

    if (head == NULL)
    {
        head = malloc(sizeof(struct zv_buffer_s) + buffer_size);
        if (head == NULL)
        {
            pthread_mutex_lock(&buffer_cache->lock);
            buffer_cache->in_use -= count;
            pthread_mutex_unlock(&buffer_cache->lock);
            // Hopefully the caller will handle out-of-memory situations properly
            return NULL;
        }

        if (size <= buffer_size)
        {
            head->next = NULL;
            return head;
        }

        count--;
        size -= buffer_size;
        tail = head;
    }

    for (;;)
    {
        struct zv_buffer_s * buffer = malloc(sizeof(struct zv_buffer_s) + buffer_size);
        if (buffer == NULL)
        {
            pthread_mutex_lock(&buffer_cache->lock);
            buffer_cache->in_use -= count;
            pthread_mutex_unlock(&buffer_cache->lock);

            tail->next = NULL;
            zv_buffer_cache_put(buffer_cache, head);
            // Hopefully the caller will handle out-of-memory situations properly
            return NULL;
        }

        tail->next = buffer;
        tail = buffer;

        if (size <= buffer_size)
        {
            tail->next = NULL;
            return head;
        }

        count--;
        size -= buffer_size;
    }
}


void zv_buffer_cache_put(zv_buffer_cache_t * buffer_cache, struct zv_buffer_s * buffer)
{
    const size_t max_size = buffer_cache->max_size;
    pthread_mutex_lock(&buffer_cache->lock);
    buffer_cache->stats.puts++;
    while (buffer_cache->size < max_size)
    {
        struct zv_buffer_s * next = buffer->next;
        buffer->next = buffer_cache->buffer;
        buffer_cache->buffer = buffer;
        buffer_cache->size++;
        buffer_cache->in_use--;
        if (next == NULL)
        {
            pthread_mutex_unlock(&buffer_cache->lock);
            return;
        }
        buffer = next;
    }

    size_t count = 0;
    for (struct zv_buffer_s * b=buffer; b!=NULL; b=b->next, count++);
    buffer_cache->in_use -= count;
    buffer_cache->stats.puts_free += count;

    pthread_mutex_unlock(&buffer_cache->lock);

    for (;;)
    {
        struct zv_buffer_s * next = buffer->next;
        free(buffer);
        if (next == NULL)
            break;
        buffer = next;
    }
}
