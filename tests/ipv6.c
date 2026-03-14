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

#include "zv/buffer.h"
#include "zv/connector.h"
#include "zv/channel.h"
#include "helpers.h"
#include <errno.h>
#include <string.h>
#include <stdint.h>

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

#if defined(_WIN32)
#include <ws2ipdef.h>
#endif

struct test_s
{
    zv_buffer_cache_t * buffer_cache;
    _Atomic size_t actions_remaining;
};


static void client_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    struct test_s * test = zv_channel_get_extra_data(channel);
    if ((size != sizeof(char)) || ((*(const char*)data) != 42))
        abort();
    zv_channel_stop(channel);
}


// use the same handler for both client and server
static void close_handler(zv_channel_t * channel)
{
    struct test_s * test = zv_channel_get_extra_data(channel);

    printf(
        "connection %s -> %s closed\n",
        zv_channel_get_local_addr_str(channel),
        zv_channel_get_peer_addr_str(channel)
    );

    size_t actions_remaining = atomic_fetch_sub(&test->actions_remaining, 1) - 1;
    if (actions_remaining == 0)
    {
        zv_loop_t * loop = zv_channel_get_loop(channel);
        zv_loop_stop(loop);
    }
}


static void after_connect(zv_connector_t * connector, zv_socket_t sockfd)
{
    zv_loop_t * loop = zv_connector_get_loop(connector);
    struct test_s * test = zv_connector_get_extra_data(connector);
    zv_connector_destroy(connector);

    zv_channel_t * channel = zv_channel_create(
        loop,
        test->buffer_cache,
        sockfd,
        &client_input_handler,
        &close_handler,
        test
    );

    if (channel)
    {
        printf(
            "connection established: %s -> %s:\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );

        zv_channel_start(channel);

        char c = 42;
        int rc = zv_channel_send(channel, &c, sizeof(c));
        zv_channel_release(channel);
    }
    else
    {
        // something really unexpected
        abort();
    }
}


static void after_connect_fail(zv_connector_t * connector, int sockerr)
{
    abort(); // should not be called
}


static void after_acceptor_stop(zv_acceptor_t * acceptor, void * arg)
{
    struct test_s * test = zv_acceptor_get_extra_data(acceptor);
    size_t actions_remaining = atomic_fetch_sub(&test->actions_remaining, 1) - 1;
    if (actions_remaining == 0)
    {
        zv_loop_t * loop = zv_acceptor_get_loop(acceptor);
        zv_loop_stop(loop);
    }
}


static void server_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    struct test_s * test = zv_channel_get_extra_data(channel);
    if ((size != sizeof(char)) || ((*(const char*)data) != 42))
        abort();
    zv_channel_send(channel, data, size);
}


static void accept_handler(zv_acceptor_t * acceptor, zv_socket_t sockfd)
{
    zv_loop_t * loop = zv_acceptor_get_loop(acceptor);
    struct test_s * test = zv_acceptor_get_extra_data(acceptor);

    zv_channel_t * channel = zv_channel_create(
        loop,
        test->buffer_cache,
        sockfd,
        &client_input_handler,
        &close_handler,
        test
    );

    if (channel)
    {
        printf(
            "connection accepted: %s -> %s:\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );

        zv_channel_start(channel);
        zv_channel_release(channel);
    }
    else
    {
        // something really unexpected
        abort();
    }

    zv_acceptor_stop(acceptor, &after_acceptor_stop, NULL);
}


int ipv6()
{
    zv_loop_t * loop = zv_loop_create(NULL);
    if (!loop)
    {
        printf("zv_loop_create() failed\n");
        return -1;
    }

    int rc = 0;
    zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(1024 * 4, 16, 4);
    if (buffer_cache)
    {
        struct test_s test;
        test.buffer_cache = buffer_cache;
        // client channel closed, server channel closed, acceptor stopped
        test.actions_remaining = 3; 

        struct sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_any;
        addr.sin6_port = 0;

        zv_acceptor_t * acceptor = zv_acceptor_create(
            loop,
            (struct sockaddr*) &addr,
            sizeof(addr),
            &accept_handler,
            &test
        );

        if (acceptor)
        {
            addr.sin6_port = htons(zv_acceptor_get_port(acceptor));
            addr.sin6_addr = in6addr_loopback;

            zv_connector_t * connector = zv_connector_create(
                loop,
                (const struct sockaddr*) &addr,
                sizeof(addr),
                &after_connect,
                &after_connect_fail,
                &test
            );

            if (connector)
            {
                zv_acceptor_start(acceptor);
                zv_connector_start(connector);
                zv_loop_run(loop);
            }
            else
            {
                printf("zv_connector_create() failed\n");
                zv_acceptor_destroy(acceptor);
                rc = -1;
            }
        }
        else
        {
            printf("zv_acceptor_create() failed\n");
            rc = -1;
        }
            
        zv_buffer_cache_release(buffer_cache);
    }
    else
    {
        printf("zv_buffer_cache_create()");
        rc = -1;
    }

    zv_loop_destroy(loop);
    return 0;
}
