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

#include "zv/atomic.h"
#include "zv/log.h"
#include "zv/loop.h"
#include "zv/os_compat.h"
#include "loop_int.h"
#include <buffer_set/buffer_set.h>

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#if defined(_WIN32)
#else
#include <sys/select.h>
#include <unistd.h>
#endif

extern struct zv_log_layer_s _zv_loop_log;

struct zv_loop_select_s
{
    struct zv_loop_s loop;
    zv_socket_t wakeup_pipe[2];
};

struct select_sockfd_info_s
{
    zv_socket_t sockfd;
    char pending_read;
    char wait_remove;
    struct zv_event_handler_s * read_handler;  // accept or read
    struct zv_event_handler_s * write_handler; // connect or write
};

static inline void _zv_select_sockfd_info_init(
    struct select_sockfd_info_s * sockfd_info,
    zv_socket_t sockfd,
    struct zv_event_handler_s * read_handler,
    struct zv_event_handler_s * write_handler
) {
    sockfd_info->sockfd = sockfd;
    sockfd_info->pending_read = 0;
    sockfd_info->wait_remove = 0;
    sockfd_info->read_handler = read_handler;
    sockfd_info->write_handler = write_handler;
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

struct fdsets_s
{
    fd_set read;
    fd_set write;
    fd_set except;
};

static int _fdsets_init(
    struct fdsets_s * fdsets,
    zv_socket_t wakeupfd,
    buffer_set_t * sockets
) {
    FD_ZERO(&fdsets->read);
    FD_ZERO(&fdsets->write);
    FD_ZERO(&fdsets->except);
    FD_SET(wakeupfd, &fdsets->read);

#if !defined(_WIN32)
    int maxfd = wakeupfd;
#endif

    buffer_set_iterator_t * it = buffer_set_begin(sockets);
    buffer_set_iterator_t * it_end = buffer_set_end(sockets);

    while (it != it_end)
    {
        const struct select_sockfd_info_s * sockfd_info = buffer_set_get_at(sockets, it);
        zv_socket_t sockfd = sockfd_info->sockfd;

        if (sockfd_info->read_handler && !sockfd_info->pending_read)
        {
    #if !defined(_WIN32)
            if (sockfd > maxfd)
                maxfd = sockfd;
    #endif
            FD_SET(sockfd, &fdsets->read);
            //printf("waiting %" PRIsockfd " for reading\n", sockfd);
        }

        if (sockfd_info->write_handler)
        {
    #if defined(_WIN32)
            // On Windows if we connect to the invalid address
            // then the socket never become writable,
            // but error reported via except fds
            if (sockfd_info->write_handler->event_type == zv_event_type_connect)
                FD_SET(sockfd, &fdsets->except);
    #else
            if (sockfd > maxfd)
                maxfd = sockfd;
    #endif
            FD_SET(sockfd, &fdsets->write);
            //printf("waiting %" PRIsocket " for writting\n", sockfd);
        }

        it = buffer_set_iterator_next(sockets, it);
    }

#if defined(_WIN32)
    return 0;
#else
    return (maxfd + 1);
#endif
}

static void _zv_loop_select_process_fdsets(
    struct fdsets_s * fdsets,
    buffer_set_t * sockets,
    zv_thread_pool_t * thread_pool,
    int * readers
) {
    buffer_set_iterator_t * it = buffer_set_begin(sockets);
    buffer_set_iterator_t * it_end = buffer_set_end(sockets);

    while (it != it_end)
    {
        buffer_set_iterator_t * it_next = buffer_set_iterator_next(sockets, it);

        struct select_sockfd_info_s * sockfd_info = buffer_set_get_at(sockets, it);
        const zv_socket_t sockfd = sockfd_info->sockfd;

        if (FD_ISSET(sockfd, &fdsets->read))
        {
            struct zv_event_handler_s * read_handler = sockfd_info->read_handler;
            sockfd_info->pending_read = 1;
            zv_thread_pool_dispatch(thread_pool, &read_handler->task);
            (*readers)++;
        }

        if (FD_ISSET(sockfd, &fdsets->write))
        {
            struct zv_event_handler_s * write_handler = sockfd_info->write_handler;
            sockfd_info->write_handler = NULL;
            zv_thread_pool_dispatch(thread_pool, &write_handler->task);

            if (!sockfd_info->read_handler)
            {
                buffer_set_erase_at(sockets, it);
                it = NULL;
            }
        }

    #if defined(_WIN32)
        if (FD_ISSET(sockfd, &fdsets->except))
        {
            struct zv_event_handler_s * write_handler = sockfd_info->write_handler;
            assert(write_handler->event_type == zv_event_type_connect);
            zv_thread_pool_dispatch(thread_pool, &write_handler->task);

            if (!sockfd_info->read_handler)
            {
                if (it != NULL)
                    buffer_set_erase_at(sockets, it);
                else
                    assert(0);
            }
        }
    #endif

        it = it_next;
    }
}

static struct zv_event_handler_s * _zv_loop_select_remove_handlers(
    zv_thread_pool_t * thread_pool,
    buffer_set_t * sockets,
    struct zv_event_handler_s * remove_handlers
) {
    struct zv_event_handler_s * prev = NULL;
    struct zv_event_handler_s * event_handler = remove_handlers;
    while (event_handler != NULL)
    {
        struct zv_event_handler_s * next = event_handler->next;
        struct select_sockfd_info_s * sockfd_info = buffer_set_get(sockets, &event_handler->sockfd);

        if (sockfd_info == NULL)
        {
            assert(0); // FIXME
        }
        else
        {
            if (!sockfd_info->pending_read || sockfd_info->wait_remove)
            {
                if (sockfd_info->write_handler)
                {
                    sockfd_info->read_handler = NULL;
                    prev = event_handler;
                }
                else
                {
                    buffer_set_erase(sockets, &event_handler->sockfd);

                    if (prev == NULL)
                        remove_handlers = next;
                    else
                        prev->next = next;
                }

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


static enum zv_loop_type_e _zv_loop_select_get_type(struct zv_loop_s * loop)
{
    return zv_loop_type_select;
}


static void _zv_loop_select_run(struct zv_loop_s * loop)
{
    struct zv_loop_select_s * impl = (struct zv_loop_select_s*) loop;
    ZV_LOG_DEBUG(_zv_loop_log, "0x%" PRIxPTR ": run", loop);

    buffer_set_t * sockets = buffer_set_create(
        sizeof(struct select_sockfd_info_s), // value size
        16, // initial capacity
        _sockfd_compare,
        NULL, // move func
        NULL  // thunk
    );

    struct fdsets_s fdsets;
    struct timeval timeout = { 0, 0 };
    int readers = 0;
    int stop = 0;

    struct zv_event_handler_s * remove_handlers = NULL;

    for (;;)
    {
        int nfds = _fdsets_init(&fdsets, impl->wakeup_pipe[0], sockets);
        int fds_ready = select(nfds, &fdsets.read, &fdsets.write, &fdsets.except, ((readers > 0) ? &timeout : NULL));
        //printf("fds_ready=%d readers=%d\n", fds_ready, readers);

        _zv_loop_select_process_fdsets(&fdsets, sockets, loop->thread_pool, &readers);

        if (FD_ISSET(impl->wakeup_pipe[0], &fdsets.read))
        {
            char c;
#if defined(_WIN32)
            int rc = recv(impl->wakeup_pipe[0], &c, sizeof(c), 0);
#else
            ssize_t rc = read(impl->wakeup_pipe[0], &c, sizeof(c));
#endif
            assert(rc == 1);
        }

        struct zv_event_handler_s * head = zv_atomic_load(&loop->head);
        while (head)
        {
            ZV_LOG_TRACE(
                _zv_loop_log,
                "loop_select_run: sockfd=%" PRIsocket " event_type=%s action=%s",
                head->sockfd,
                zv_event_type_get_name(head->event_type),
                zv_event_opcode_get_name(head->opcode)
            );

            switch (head->event_type)
            {
            case zv_event_type_accept:
            case zv_event_type_read:
                if (head->opcode == zv_event_opcode_add)
                {
                    int inserted;
                    struct select_sockfd_info_s * sockfd_info = buffer_set_insert(sockets, &head->sockfd, &inserted);
                    assert(inserted);
                    _zv_select_sockfd_info_init(sockfd_info, head->sockfd, head, NULL);
                }
                else if (head->opcode == zv_event_opcode_resume)
                {
                    struct select_sockfd_info_s * sockfd_info = buffer_set_get(sockets, &head->sockfd);
                    assert(sockfd_info->read_handler == head);
                    assert(sockfd_info->pending_read);
                    sockfd_info->pending_read = 0;
                    readers--;
                }
                else if (head->opcode == zv_event_opcode_resume_remove)
                {
                    struct select_sockfd_info_s * sockfd_info = buffer_set_get(sockets, &head->sockfd);
                    assert(sockfd_info->read_handler == head);
                    assert(sockfd_info->pending_read);
                    if (sockfd_info->write_handler == NULL)
                        buffer_set_erase(sockets, &head->sockfd);
                    else
                    {
                        sockfd_info->read_handler = NULL;
                        sockfd_info->pending_read = 0;
                    }
                    readers--;
                    zv_thread_pool_dispatch(loop->thread_pool, &head->task);
                }
                else if (head->opcode == zv_event_opcode_resume_wait_remove)
                {
                    struct select_sockfd_info_s * sockfd_info = buffer_set_get(sockets, &head->sockfd);
                    assert(sockfd_info->read_handler == head);
                    assert(sockfd_info->pending_read);
                    assert(sockfd_info->wait_remove == 0);
                    sockfd_info->wait_remove = 1;
                    readers--;
                }
                else if (head->opcode == zv_event_opcode_remove)
                {
                    // can't modify head->next while the event handler is in the queue,
                    // will do it after _zv_loop_pop()
                }
                else
                    assert(0);
            break;

            case zv_event_type_connect:
            case zv_event_type_write:
                if (head->opcode == zv_event_opcode_add)
                {
                    int inserted;
                    struct select_sockfd_info_s * sockfd_info;
                    sockfd_info = buffer_set_insert(sockets, &head->sockfd, &inserted);
                    if (inserted)
                        _zv_select_sockfd_info_init(sockfd_info, head->sockfd, NULL, head);
                    else
                    {
                        assert(!sockfd_info->write_handler);
                        sockfd_info->write_handler = head;
                    }
                }
                else
                    assert(0);
            break;

            default:
                assert(0);
            }

            struct zv_event_handler_s * next = _zv_loop_pop(loop, head);
            if (head->opcode == zv_event_opcode_remove)
            {
                assert((head->event_type == zv_event_type_accept) || (head->event_type == zv_event_type_read));
                head->next = remove_handlers;
                remove_handlers = head;
            }

            head = next;
        }

        struct zv_event_handler_s * tail_n_flag = zv_atomic_load(&loop->tail_n_flag);
        if (((uintptr_t)tail_n_flag) & 1)
        {
            //if (buffer_set_get_size(handlers) == 0)
            stop = 1;
        }

        remove_handlers = _zv_loop_select_remove_handlers(loop->thread_pool, sockets, remove_handlers);

        if (stop && (readers == 0))
            break;
    }

    buffer_set_destroy(sockets);

    ZV_LOG_DEBUG(_zv_loop_log, "0x%" PRIxPTR ": run done", loop);
}


static void _zv_loop_select_wakeup(struct zv_loop_s * loop)
{
    struct zv_loop_select_s * impl = (struct zv_loop_select_s*) loop;
    char c = 0;
#if defined(_WIN32)
    send(impl->wakeup_pipe[1], &c, sizeof(c), 0);
#else
    write(impl->wakeup_pipe[1], &c, sizeof(c));
#endif
}


static void _zv_loop_select_deinit(struct zv_loop_s * loop)
{
    struct zv_loop_select_s * impl = (struct zv_loop_select_s*) loop;
    zv_socket_close(impl->wakeup_pipe[0]);
    zv_socket_close(impl->wakeup_pipe[1]);
}


struct zv_loop_s * _zv_loop_create_select(const struct zv_loop_config_s * config)
{
    zv_socket_t wakeup_pipe[2];
    int rc = zv_pipe(wakeup_pipe);
    if (rc != 0)
        return NULL;

    struct zv_loop_select_s * impl = malloc(sizeof(struct zv_loop_select_s));
    if (impl)
    {
        int rc = _zv_loop_init(
            &impl->loop,
            config,
            &_zv_loop_select_get_type,
            &_zv_loop_select_run,
            &_zv_loop_select_wakeup,
            &_zv_loop_select_deinit
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
