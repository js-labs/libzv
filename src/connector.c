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

#include "zv/connector.h"
#include "zv/log.h"
#include "zv/loop.h"
#include "zv/thread_pool.h"
#include "loop_int.h"
#include "misc.h"
#include <string.h>

#if !defined(_WIN32)
#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>
#endif

static struct zv_log_layer_s _zv_connector_log = { "zv.connector", zv_log_level_warning };

struct zv_connector_s
{
    zv_loop_t * loop;

    struct zv_event_handler_s handler;

    socklen_t addrlen;
    // address will be stored after the zv_connector_s struct
    // struct sockaddr * addr;

    void (*after_connect)(zv_connector_t*, zv_socket_t);
    void (*after_fail)(zv_connector_t*, int errcode);
    void * extra_data;
};


static int _zv_err_in_progress(int rc)
{
#if defined(_WIN32)
    return (rc == WSAEWOULDBLOCK);
#elif defined(__linux__) || defined(__APPLE__)
    return (rc == EINPROGRESS);
#else
#error Not implemented
#endif
}


static struct sockaddr * _zv_connector_get_addr(struct zv_connector_s * connector)
{
    const size_t connector_size = _zv_round(sizeof(struct zv_connector_s), sizeof(void*));
    return (struct sockaddr*) (((char*)connector) + connector_size);
}


static void _zv_connector_connect_task(struct zv_thread_pool_task_s * task)
{
    struct zv_connector_s * connector = _ZV_CONTAINER_OF(
        _ZV_CONTAINER_OF(task, struct zv_event_handler_s, task),
        struct zv_connector_s,
        handler
    );
    zv_socket_t sockfd = connector->handler.sockfd;

    int sockerr;
    socklen_t optlen = sizeof(sockerr);
    int rc = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char*) &sockerr, &optlen);
    if (rc == 0)
    {
        if (sockerr == 0)
        {
            connector->after_connect(connector, sockfd);
            free(connector);
            return;
        }
    }
    else
        sockerr = _zv_get_errno();

    zv_socket_close(sockfd);
    connector->after_fail(connector, sockerr);
    free(connector);
}


static void _zv_connector_task(struct zv_thread_pool_task_s * task)
{
    struct zv_connector_s * connector = _ZV_CONTAINER_OF(
        _ZV_CONTAINER_OF(task, struct zv_event_handler_s, task),
        struct zv_connector_s,
        handler
    );
    struct sockaddr * addr = _zv_connector_get_addr(connector);

    zv_socket_t sockfd = socket(addr->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET)
    {
        const int sockerr = _zv_get_errno();
        connector->after_fail(connector, sockerr);
        free(connector);
        return;
    }

#if defined(_WIN32)
    u_long nonblock = 1;
    int rc = ioctlsocket(sockfd, FIONBIO, &nonblock);
#else
    int rc = fcntl(sockfd, F_SETFL, O_NONBLOCK);
#endif
    if (rc != 0)
    {
        const int sockerr = _zv_get_errno();
        zv_socket_close(sockfd);
        connector->after_fail(connector, sockerr);
        free(connector);
        return;
    }

    rc = connect(sockfd, addr, connector->addrlen);
    if (rc == 0)
    {
        // sometimes it happens
        connector->after_connect(connector, sockfd);
    }
    else
    {
        int sockerr = _zv_get_errno();
        if (_zv_err_in_progress(sockerr))
        {
            _zv_event_handler_init(
                &connector->handler,
                sockfd,
                zv_event_type_connect,
                zv_event_opcode_add,
                &_zv_connector_connect_task
            );

            rc = _zv_loop_add_handler(connector->loop, &connector->handler);
            if (rc == 0)
                return;

#if defined(_WIN32)
            sockerr = WSAECONNABORTED;
#else
            sockerr = ECONNABORTED;
#endif
        }
        zv_socket_close(sockfd);
        connector->after_fail(connector, sockerr);
    }
    free(connector);
}

int zv_connector_create(
    zv_loop_t * loop,
    const struct sockaddr * addr,
    socklen_t addrlen,
    void (*after_connect)(zv_connector_t*, zv_socket_t),
    void (*after_fail)(zv_connector_t*, int /*sockerr*/),
    void * extra_data
) {
    const size_t connector_size = _zv_round(sizeof(struct zv_connector_s), sizeof(void*));
    struct zv_connector_s * connector = malloc(connector_size + addrlen);
    if (connector == NULL)
        return ENOMEM;

    connector->loop = loop;
    connector->addrlen = addrlen;
    memcpy(((char*)connector) + connector_size, addr, addrlen);
    connector->after_connect = after_connect;
    connector->after_fail = after_fail;
    connector->extra_data = extra_data;

    zv_thread_pool_task_init(&connector->handler.task, &_zv_connector_task);
    zv_thread_pool_dispatch(loop->thread_pool, &connector->handler.task);
    return 0;
}

zv_loop_t * zv_connector_get_loop(zv_connector_t * connector)
{
    return connector->loop;
}

void * zv_connector_get_extra_data(zv_connector_t * connector)
{
    return connector->extra_data;
}

const struct sockaddr * zv_connector_get_addr(zv_connector_t * connector, socklen_t * addrlen)
{
    *addrlen = connector->addrlen;
    return _zv_connector_get_addr(connector);
}

void zv_connector_set_log_level(enum zv_log_level level)
{
    _zv_connector_log.level = level;
}
