/*
 * Copyright (C) 2025 Sergey Zubarev, info@js-labs.org
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

#if !defined(__APPLE__)

#include "buffer_set/buffer_set.h"
#include "zv/loop.h"
#include "zv/os_compat.h"
#include <assert.h>

#include "loop_int.h"
#include "misc.h"

#if defined(__linux__)
#include <poll.h>
#include <string.h>
#endif

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

struct zv_loop_poll_s
{
    struct zv_loop_s loop;
    zv_socket_t wakeup_pipe[2];
};

extern struct zv_log_layer_s _zv_loop_log;

struct poll_sockfd_info_s
{
    zv_socket_t sockfd;
    size_t idx;
    char wait_remove;
    char pending_read;
    struct zv_event_handler_s * read_handler;  // accept or read
    struct zv_event_handler_s * write_handler; // connect or write
};

static inline void _zv_poll_sockfd_info_init(
    struct poll_sockfd_info_s * sockfd_info,
    zv_socket_t sockfd,
    size_t idx,
    struct zv_event_handler_s * read_handler,
    struct zv_event_handler_s * write_handler
) {
    sockfd_info->sockfd = sockfd;
    sockfd_info->idx = idx;
    sockfd_info->wait_remove = 0;
    sockfd_info->pending_read = 0;
    sockfd_info->read_handler = read_handler;
    sockfd_info->write_handler = write_handler;
}

struct pollfd_array_s
{
    size_t size;
    size_t capacity;
    struct pollfd * buffer;
};

void _zv_pollfd_array_init(struct pollfd_array_s * array, size_t capacity)
{
    array->size = 0;
    array->capacity = capacity;
    array->buffer = malloc(sizeof(struct pollfd) * capacity);
}

void _zv_pollfd_array_deinit(struct pollfd_array_s * array)
{
    free(array->buffer);
}

static size_t _zv_pollfd_array_append(struct pollfd_array_s * array)
{
    if (array->size == array->capacity)
    {
        array->capacity *= 2;
        struct pollfd * buffer = malloc(sizeof(struct pollfd) * array->capacity);
        memcpy(buffer, array->buffer, sizeof(struct pollfd) * array->size);
        free(array->buffer);
        array->buffer = buffer;
    }
    size_t idx = array->size;
    array->size++;
    return idx;
}


static void _zv_pollfd_array_remove(
    struct pollfd_array_s * array,
    size_t idx,
    buffer_set_t * sockets
) {
    // the first item is always wakeup pipe/socket
    assert(array->size > 1);
    size_t size = (array->size - 1);
    if ((size > 1) && (idx < size))
    {
        struct poll_sockfd_info_s * sockfd_info = buffer_set_get(sockets, &array->buffer[size].fd);
        sockfd_info->idx = idx;
        array->buffer[idx] = array->buffer[size];
    }
    array->size = size;
}


static int _sockfd_compare(const void * v1, const void * v2, void * thunk)
{
    zv_socket_t s1 = *((const zv_socket_t*) v1);
    zv_socket_t s2 = *((const zv_socket_t*) v2);
    if (s1 < s2)
        return -1;
    else if (s2 < s1)
        return 1;
    else
        return 0;
}


static struct zv_event_handler_s * _zv_loop_poll_remove_handlers(
    zv_thread_pool_t * thread_pool,
    buffer_set_t * sockets,
    struct zv_event_handler_s * remove_handlers,
    struct pollfd_array_s * pollfd_array
) {
    struct zv_event_handler_s * prev = NULL;
    struct zv_event_handler_s * event_handler = remove_handlers;
    while (event_handler != NULL)
    {
        struct zv_event_handler_s * next = event_handler->next;
        struct poll_sockfd_info_s * sockfd_info = buffer_set_get(sockets, &event_handler->sockfd);

        if (sockfd_info == NULL)
        {
            abort(); // FIXME
        }
        else
        {
            struct pollfd * pollfd = &pollfd_array->buffer[sockfd_info->idx];
            int waiting_read = (pollfd->events & POLLIN);
            if (waiting_read || sockfd_info->wait_remove)
            {
                if (sockfd_info->write_handler)
                {
                    sockfd_info->read_handler = NULL;
                    prev = event_handler;
                }
                else
                {
                    _zv_pollfd_array_remove(pollfd_array, sockfd_info->idx, sockets);
                    buffer_set_erase(sockets, &event_handler->sockfd);

                    if (prev == NULL)
                        remove_handlers = next;
                    else
                        prev->next = next;
                }

                event_handler->next = NULL;
                zv_thread_pool_dispatch(thread_pool, &event_handler->task);
            }
            else
            {
                // read operation is in progress now,
                // let's wait for the next loop iteration
                prev = event_handler;
            }
        }
        event_handler = next;
    }
    return remove_handlers;
}


static enum zv_loop_type_e _zv_loop_poll_get_type(struct zv_loop_s * loop)
{
    return zv_loop_type_poll;
}


static void _zv_loop_poll_run(struct zv_loop_s * loop)
{
    ZV_LOG_DEBUG(_zv_loop_log, "poll_run");

    struct zv_loop_poll_s * impl = (struct zv_loop_poll_s*) loop;
    zv_thread_pool_t * thread_pool = loop->thread_pool;

    buffer_set_t * sockets = buffer_set_create(
        sizeof(struct poll_sockfd_info_s), // value size
        16, // initial capacity
        _sockfd_compare,
        NULL, // move func
        NULL  // thunk
    );

    struct pollfd_array_s pollfd_array;
    _zv_pollfd_array_init(&pollfd_array, 16);

    pollfd_array.size = 1;
    pollfd_array.buffer[0].fd = impl->wakeup_pipe[0];
    pollfd_array.buffer[0].events = POLLIN;
    pollfd_array.buffer[0].revents = 0;

    struct zv_event_handler_s * remove_handlers = NULL;
    size_t readers = 0;
    int stop = 0;

    for (;;)
    {
#if defined(_WIN32)
        const int timeout = (readers > 0) ? 0 : INFINITE;
        int rc = WSAPoll(pollfd_array.buffer, (ULONG) pollfd_array.size, timeout);
        if (rc < 0)
        {
            rc = WSAGetLastError();
           //continue;
        }
#else
        const int timeout = (readers > 0) ? 0 : -1;
        int rc = poll(pollfd_array.buffer, pollfd_array.size, timeout);
#endif

        for (size_t idx=1; idx<pollfd_array.size;)
        {
            struct pollfd * pollfd = &pollfd_array.buffer[idx];

            if (zv_log_level_enabled(&_zv_loop_log, zv_log_level_trace))
            {
                if (pollfd->revents != 0)
                {
                    char buffer[128];
                    int len = snprintf(buffer, sizeof(buffer), "socket %" PRIsocket "[%zd]: revents=", pollfd->fd, idx);
                    if (pollfd->revents & POLLIN)
                        len += snprintf(buffer + len, (sizeof(buffer) - len), "+IN");
                    if (pollfd->revents & POLLOUT)
                        len += snprintf(buffer + len, (sizeof(buffer) - len), "+OUT");
                    if (pollfd->revents & POLLERR)
                        len += snprintf(buffer + len, (sizeof(buffer) - len), "+ERR");
                    if (pollfd->revents & POLLHUP)
                        len += snprintf(buffer + len, (sizeof(buffer) - len), "+HUP");
                    ZV_LOG_TRACE(_zv_loop_log, buffer);
                }
            }

            struct poll_sockfd_info_s * sockfd_info = buffer_set_get(sockets, &pollfd->fd);
            if (sockfd_info == NULL)
            {
                assert(0);
                continue;
            }

            if (pollfd->revents & POLLHUP)
            {
                // pollfd_array.buffer[0] is a wakeup socket,
                // so we can use 'idx' == 0 to indicate socket error.
                sockfd_info->idx = 0;

                struct zv_event_handler_s * read_handler = sockfd_info->read_handler;
                if (read_handler && !sockfd_info->pending_read)
                {
                    sockfd_info->pending_read = 1;
                    zv_thread_pool_dispatch(thread_pool, &read_handler->task);
                    readers++;
                }

                struct zv_event_handler_s * write_handler = sockfd_info->write_handler;
                if (sockfd_info->write_handler)
                {
                    sockfd_info->write_handler = NULL;
                    zv_thread_pool_dispatch(thread_pool, &write_handler->task);
                }

                _zv_pollfd_array_remove(&pollfd_array, idx, sockets);
            }
            else
            {
                if (pollfd->revents & POLLIN)
                {
                    sockfd_info->pending_read = 1;
                    struct zv_event_handler_s * read_handler = sockfd_info->read_handler;
                    zv_thread_pool_dispatch(thread_pool, &read_handler->task);
                    pollfd->events -= POLLIN;
                    readers++;
                }

                if (pollfd->revents & POLLOUT)
                {
                    struct zv_event_handler_s * write_handler = sockfd_info->write_handler;
                    zv_thread_pool_dispatch(thread_pool, &write_handler->task);
                    pollfd->events -= POLLOUT;
                    sockfd_info->write_handler = NULL;
                }

                if ((pollfd->events == 0) && (sockfd_info->read_handler == NULL))
                {
                    buffer_set_erase(sockets, &pollfd->fd);
                    _zv_pollfd_array_remove(&pollfd_array, idx, sockets);
                }
                else
                {
                    pollfd->revents = 0;
                    idx++;
                }
            }
        }

        if (pollfd_array.buffer[0].revents & POLLIN)
        {
            struct pollfd * pollfd = &pollfd_array.buffer[0];
            char c;
#if defined(_WIN32)
            int rc = recv(pollfd->fd, &c, sizeof(c), 0);
#else
            ssize_t rc = read(pollfd->fd, &c, sizeof(c));
#endif
            if (rc < 0)
            {
                // not expected
                ZV_LOG_ERROR(
                    _zv_loop_log,
                    "read(%" PRIsocket ") failed: %s",
                    pollfd->fd, strerror(errno)
                );
            }
            pollfd->revents = 0;
        }

        struct zv_event_handler_s * head = atomic_load(&loop->head);
        while (head)
        {
            if (zv_log_level_enabled(&_zv_loop_log, zv_log_level_debug))
            {
                if ((head->event_type == zv_event_type_read) && (head->opcode == zv_event_opcode_resume))
                {
                    ZV_LOG_TRACE(
                        _zv_loop_log,
                        "sockfd=%" PRIsocket " event_type=%s opcode=%s",
                        head->sockfd,
                        zv_event_type_get_name(head->event_type),
                        zv_event_opcode_get_name(head->opcode)
                    );
                }
                else
                {
                    ZV_LOG_TRACE(
                        _zv_loop_log,
                        "sockfd=%" PRIsocket " event_type=%s opcode=%s",
                        head->sockfd,
                        zv_event_type_get_name(head->event_type),
                        zv_event_opcode_get_name(head->opcode)
                    );
                }
            }

            struct zv_event_handler_s * next = _zv_loop_pop(loop, head);
            switch (head->event_type)
            {
                case zv_event_type_accept:
                case zv_event_type_read:
                    if (head->opcode == zv_event_opcode_add)
                    {
                        int inserted;
                        struct poll_sockfd_info_s * sockfd_info = buffer_set_insert(sockets, &head->sockfd, &inserted);
                        assert(inserted);
                        size_t idx = _zv_pollfd_array_append(&pollfd_array);
                        _zv_poll_sockfd_info_init(sockfd_info, head->sockfd, idx, head, NULL);
                        struct pollfd * pollfd = &pollfd_array.buffer[idx];
                        pollfd->fd = head->sockfd;
                        pollfd->events = POLLIN;
                        pollfd->revents = 0;
                    }
                    else if (head->opcode == zv_event_opcode_resume)
                    {
                        struct poll_sockfd_info_s * sockfd_info = buffer_set_get(sockets, &head->sockfd);
                        assert(sockfd_info->read_handler == head);
                        assert(sockfd_info->pending_read);
                        size_t idx = sockfd_info->idx;
                        if (idx == 0)
                        {
                            // event handler will try to read socket and will get an error
                            zv_thread_pool_dispatch(thread_pool, &head->task);
                        }
                        else
                        {
                            sockfd_info->pending_read = 0;
                            struct pollfd * pollfd = &pollfd_array.buffer[idx];
                            assert((pollfd->events & POLLIN) == 0);
                            pollfd->events |= POLLIN;
                            readers--;
                        }
                    }
                    else if (head->opcode == zv_event_opcode_resume_remove)
                    {
                        struct poll_sockfd_info_s * sockfd_info = buffer_set_get(sockets, &head->sockfd);
                        assert(sockfd_info->read_handler == head);
                        assert(sockfd_info->pending_read);
                        size_t idx = sockfd_info->idx;
                        if (idx == 0)
                        {
                            assert(sockfd_info->write_handler == NULL);
                            buffer_set_erase(sockets, &head->sockfd);
                        }
                        else
                        {
                            struct pollfd * pollfd = &pollfd_array.buffer[idx];
                            assert(pollfd->fd == sockfd_info->sockfd);
                            assert((pollfd->events & POLLIN) == 0);
                            if (sockfd_info->write_handler == NULL)
                            {
                                _zv_pollfd_array_remove(&pollfd_array, idx, sockets);
                                buffer_set_erase(sockets, &head->sockfd);
                            }
                            else
                                sockfd_info->read_handler = NULL;
                        }

                        zv_thread_pool_dispatch(thread_pool, &head->task);
                        readers--;
                    }
                    else if (head->opcode == zv_event_opcode_resume_wait_remove)
                    {
                        struct poll_sockfd_info_s * sockfd_info = buffer_set_get(sockets, &head->sockfd);
                        assert(sockfd_info->read_handler == head);
                        assert(sockfd_info->pending_read);
                        assert(sockfd_info->wait_remove == 0);
                        size_t idx = sockfd_info->idx;
                        if (idx != 0)
                        {
                            assert((pollfd_array.buffer[idx].events & POLLIN) == 0);
                        }
                        sockfd_info->wait_remove = 1;
                        readers--;
                    }
                    else if (head->opcode == zv_event_opcode_remove)
                    {
                        assert((head->event_type == zv_event_type_accept) || (head->event_type == zv_event_type_read));
                        head->next = remove_handlers;
                        remove_handlers = head;
                    }
                    else
                        assert(0);
                break;

                case zv_event_type_connect:
                case zv_event_type_write:
                    if (head->opcode == zv_event_opcode_add)
                    {
                        int inserted;
                        struct poll_sockfd_info_s * sockfd_info = buffer_set_insert(sockets, &head->sockfd, &inserted);
                        if (inserted)
                        {
                            size_t idx = _zv_pollfd_array_append(&pollfd_array);
                            _zv_poll_sockfd_info_init(sockfd_info, head->sockfd, idx, NULL, head);
                            struct pollfd * pollfd = &pollfd_array.buffer[idx];
                            pollfd->fd = head->sockfd;
                            pollfd->events = POLLOUT;
                            pollfd->revents = 0;
                        }
                        else
                        {
                            assert(!sockfd_info->write_handler);
                            size_t idx = sockfd_info->idx;
                            if (idx == 0)
                                zv_thread_pool_dispatch(thread_pool, &head->task);
                            else
                            {
                                sockfd_info->write_handler = head;
                                struct pollfd * pollfd = &pollfd_array.buffer[idx];
                                assert(pollfd->fd == head->sockfd);
                                pollfd->events |= POLLOUT;
                            }
                        }
                    }
                    else
                        assert(0);
                    break;

                default:
                    assert(0);
            }
            head = next;
        }

        struct zv_event_handler_s * tail_n_flag = atomic_load(&loop->tail_n_flag);
        if (((uintptr_t)tail_n_flag) & 1)
        {
            //if (buffer_set_get_size(handlers) == 0)
            stop = 1;
        }

        remove_handlers = _zv_loop_poll_remove_handlers(
            loop->thread_pool,
            sockets,
            remove_handlers,
            &pollfd_array
        );

        if (stop && (readers == 0))
            break;
    }

    _zv_pollfd_array_deinit(&pollfd_array);
    buffer_set_destroy(sockets);

    ZV_LOG_DEBUG(_zv_loop_log, "poll_run: done");
}


static void _zv_loop_poll_wakeup(zv_loop_t * loop)
{
    struct zv_loop_poll_s * impl = (struct zv_loop_poll_s*) loop;
    char c = 0;
#if defined(_WIN32)
    int rc = send(impl->wakeup_pipe[1], &c, sizeof(c), 0);
#else
    ssize_t rc = write(impl->wakeup_pipe[1], &c, sizeof(c));
#endif
    if (rc < 0)
    {
        // should never happen
        ZV_LOG_ERROR(
            _zv_loop_log,
            "write(%" PRIsocket ") failed: %s",
            impl->wakeup_pipe[1], strerror(errno)
        );
    }
}


static void _zv_loop_poll_deinit(struct zv_loop_s * loop)
{
    struct zv_loop_poll_s * impl = (struct zv_loop_poll_s*) loop;
    zv_socket_close(impl->wakeup_pipe[0]);
    zv_socket_close(impl->wakeup_pipe[1]);
}


struct zv_loop_s * _zv_loop_create_poll(const struct zv_loop_config_s * config)
{
    zv_socket_t wakeup_pipe[2];
    int rc = zv_pipe(wakeup_pipe);
    if (rc != 0)
        return NULL;

    struct zv_loop_poll_s * impl = malloc(sizeof(struct zv_loop_poll_s));
    if (impl)
    {
        int rc = _zv_loop_init(
            &impl->loop,
            config,
            &_zv_loop_poll_get_type,
            &_zv_loop_poll_run,
            &_zv_loop_poll_wakeup,
            &_zv_loop_poll_deinit
        );

        if (rc == 0)
        {
            impl->wakeup_pipe[0] = wakeup_pipe[0];
            impl->wakeup_pipe[1] = wakeup_pipe[1];
            return &impl->loop;
        }

        free(impl);
    }

    zv_socket_close(wakeup_pipe[0]);
    zv_socket_close(wakeup_pipe[1]);

    return NULL;
}

#endif /* !defined(__APPLE__) */
