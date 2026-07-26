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

#include "zv/acceptor.h"
#include "zv/log.h"
#include "zv/loop.h"
#include "zv/thread_pool.h"
#include "loop_int.h"
#include "misc.h"

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#endif
#include <inttypes.h>

static struct zv_log_layer_s _zv_acceptor_log = { "zv.acceptor", zv_log_level_warning };

struct zv_acceptor_s
{
    zv_loop_t * loop;
    struct zv_event_handler_s handler;
    in_port_t port;
    void (*accept_handler)(zv_acceptor_t * acceptor, zv_socket_t sockfd);
    void * extra_data;
};

struct zv_acceptor_stop_s
{
    struct zv_acceptor_s * acceptor;
    struct zv_event_handler_s handler;
    void (*after_stop)(zv_acceptor_t*, void*);
    void * after_stop_arg;
};

static void _zv_acceptor_accept_task(struct zv_thread_pool_task_s * task)
{
    struct zv_acceptor_s * acceptor = _ZV_CONTAINER_OF(
        _ZV_CONTAINER_OF(task, struct zv_event_handler_s, task),
        struct zv_acceptor_s,
        handler
    );

    for (;;)
    {
        zv_socket_t sockfd = accept(acceptor->handler.sockfd, NULL, NULL);
        if (sockfd == INVALID_SOCKET)
        {
            int last_error = _zv_get_errno();
#if defined(_WIN32)
            if (last_error != WSAEWOULDBLOCK)
#else
            if (last_error != EAGAIN)
#endif
            {
                ZV_LOG_ERROR(
                    _zv_acceptor_log,
                    "0x%" PRIxPTR ": accept() failed: %d",
                    acceptor, last_error
                );
            }
            break;
        }
        acceptor->accept_handler(acceptor, sockfd);
    }
    acceptor->handler.opcode = zv_event_opcode_resume;
    _zv_loop_resume_handler(acceptor->loop, &acceptor->handler);
}


static int _get_port(zv_socket_t sockfd, in_port_t * port)
{
    struct sockaddr_storage addr_storage;
    struct sockaddr * addr = (struct sockaddr*) &addr_storage;
    socklen_t addrlen = sizeof(addr_storage);
    int rc = getsockname(sockfd, addr, &addrlen);
    if (rc != 0)
    {
        // On Windows in a case if the buffer size is too small
        // then getsockname() fails and error code will be WSAEFAULT
        // No way to get the required buffer size in that case.
        // Hope sizeof(sockaddr_storage) will be always enough.
        return -1;
    }

    if (addr->sa_family == AF_INET)
    {
        const struct sockaddr_in * in_addr = (void*) addr;
        *port = ntohs(in_addr->sin_port);
    }
    else if (addr->sa_family == AF_INET6)
    {
        const struct sockaddr_in6 * in6_addr = (void*) addr;
        *port = ntohs(in6_addr->sin6_port);
    }
    else
    {
        _zv_set_errno(EINVAL);
        rc = -1;
    }

    return rc;
}


zv_acceptor_t * zv_acceptor_create(
    zv_loop_t * loop,
    const struct sockaddr * addr,
    socklen_t addrlen,
    void (*accept_handler)(zv_acceptor_t*, zv_socket_t),
    void * extra_data
) {
    zv_socket_t sockfd = socket(addr->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET)
        return NULL;

    int rc = bind(sockfd, addr, addrlen);
    if (rc == 0)
    {
        in_port_t port;
        rc = _get_port(sockfd, &port);
        if (rc != 0)
        {
            const int errcode = _zv_get_errno();
            zv_socket_close(sockfd);
            _zv_set_errno(errcode);
            return NULL;
        }

#if defined(_WIN32)
        u_long mode = 1;
        rc = ioctlsocket(sockfd, FIONBIO, &mode);
#else
        rc = fcntl(sockfd, F_SETFL, O_NONBLOCK);
#endif
        if (rc == 0)
        {
            rc = listen(sockfd, 10);
            if (rc == 0)
            {
                struct zv_acceptor_s * acceptor = malloc(sizeof(struct zv_acceptor_s));
                if (acceptor == NULL)
                {
                    zv_socket_close(sockfd);
                    _zv_set_errno(ENOMEM);
                    return NULL;
                }

                acceptor->loop = loop;

                _zv_event_handler_init(
                    &acceptor->handler,
                    sockfd,
                    zv_event_type_accept,
                    zv_event_opcode_add,
                    &_zv_acceptor_accept_task
                );

                acceptor->port = port;
                acceptor->accept_handler = accept_handler;
                acceptor->extra_data = extra_data;

                ZV_LOG_DEBUG(
                    _zv_acceptor_log,
                    "0x%" PRIxPTR ": sockfd=%" PRIsocket ", port=%u",
                    acceptor, sockfd, port
                );

                return acceptor;
            }
        }
    }

    rc = _zv_get_errno();
    zv_socket_close(sockfd);
    _zv_set_errno(rc);

    return NULL;
}


zv_loop_t * zv_acceptor_get_loop(zv_acceptor_t * acceptor)
{
    return acceptor->loop;
}


in_port_t zv_acceptor_get_port(zv_acceptor_t * acceptor)
{
    return acceptor->port;
}


void * zv_acceptor_get_extra_data(zv_acceptor_t * acceptor)
{
    return acceptor->extra_data;
}


int zv_acceptor_start(zv_acceptor_t * acceptor)
{
    return _zv_loop_add_handler(acceptor->loop, &acceptor->handler);
}


static void _zv_acceptor_remove_task(struct zv_thread_pool_task_s * task)
{
    struct zv_acceptor_stop_s * acceptor_stop = _ZV_CONTAINER_OF(
        _ZV_CONTAINER_OF(task, struct zv_event_handler_s, task),
        struct zv_acceptor_stop_s,
        handler
    );
    zv_acceptor_t * acceptor = acceptor_stop->acceptor;
    void (*after_stop)(zv_acceptor_t*, void*) = acceptor_stop->after_stop;
    void * after_stop_arg = acceptor_stop->after_stop_arg;
    free(acceptor_stop);
    after_stop(acceptor, after_stop_arg);
}


int zv_acceptor_stop(
    zv_acceptor_t * acceptor,
    void (*after_stop)(zv_acceptor_t*, void*),
    void * after_stop_arg
) {
    struct zv_acceptor_stop_s * acceptor_stop = malloc(sizeof(struct zv_acceptor_stop_s));
    if (acceptor_stop)
    {
        acceptor_stop->acceptor = acceptor;

        _zv_event_handler_init(
            &acceptor_stop->handler,
            acceptor->handler.sockfd,
            zv_event_type_accept,
            zv_event_opcode_remove,
            &_zv_acceptor_remove_task
        );

        acceptor_stop->after_stop = after_stop;
        acceptor_stop->after_stop_arg = after_stop_arg;

        _zv_loop_add_handler(acceptor->loop, &acceptor_stop->handler);
        return 0;
    }
    else
        return -1;
}


void zv_acceptor_destroy(zv_acceptor_t * acceptor)
{
    zv_socket_close(acceptor->handler.sockfd);
    free(acceptor);
}


void zv_acceptor_set_log_level(enum zv_log_level level)
{
    _zv_acceptor_log.level = level;
}
