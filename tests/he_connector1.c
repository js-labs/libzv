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

#include "zv/dns_resolver.h"
#include "zv/he_connector.h"
#include "zv/loop.h"
#include "zv/semaphore.h"
#include "helpers.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#endif

/* Builds a linked list of <count> IPv4 addrinfo entries.
 * Each entry is given by a pair of variadic arguments:
 * the address in host byte order (uint32_t) followed by
 * the port in host byte order (in_port_t).
 */
struct addrinfo * make_test_addrs(size_t count, ...)
{
    if (count == 0)
        return NULL;

    size_t size = (count * sizeof(struct addrinfo));
    size += (count * sizeof(struct sockaddr_in));

    char * buffer = malloc(size);
    if (buffer == NULL)
        return NULL;

    struct addrinfo * ai = (struct addrinfo*) buffer;
    struct sockaddr_in * sa = (struct sockaddr_in*) (ai + count);

    va_list ap;
    va_start(ap, count);

    for (size_t idx=0; idx<count; idx++)
    {
        /* in_port_t is promoted to int when passed through '...'. */
        const uint32_t addr = va_arg(ap, uint32_t);
        const in_port_t port = (in_port_t) va_arg(ap, int);

        memset(&ai[idx], 0, sizeof(ai[idx]));
        memset(&sa[idx], 0, sizeof(sa[idx]));

        sa[idx].sin_family = AF_INET;
        sa[idx].sin_addr.s_addr = htonl(addr);
        sa[idx].sin_port = htons(port);

        ai[idx].ai_family = AF_INET;
        ai[idx].ai_socktype = SOCK_STREAM;
        ai[idx].ai_protocol = IPPROTO_TCP;
        ai[idx].ai_addrlen = sizeof(struct sockaddr_in);
        ai[idx].ai_addr = (struct sockaddr*) &sa[idx];
        ai[idx].ai_next = &ai[idx + 1];
    }
    ai[count - 1].ai_next = NULL;

    va_end(ap);

    return ai;
}


static void after_connect(zv_he_connector_t * he_connector, zv_socket_t sockfd)
{
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    int rc = getpeername(sockfd, (struct sockaddr*)&addr, &addrlen);
    if (rc == 0)
    {
        char buffer[INET_ADDRSTRLEN + 6];
        zv_sockaddr_ntop(
            (const struct sockaddr*) &addr,
            addrlen,
            buffer,
            sizeof(buffer)
        );

        printf(
            "connected to '%s' @ %s\n",
            zv_he_connector_get_host(he_connector),
            buffer
        );
    }
    else
        fprintf(stderr, "getpeername() failed\n");

    zv_socket_close(sockfd);

    zv_loop_t * loop = zv_he_connector_get_loop(he_connector);
    zv_loop_stop(loop);
}


static void after_fail(zv_he_connector_t * he_connector, int errcode)
{
    zv_loop_t * loop = zv_he_connector_get_loop(he_connector);
    zv_loop_stop(loop);
}


static void after_accept(zv_socket_t sockfd, void * thunk)
{
    zv_socket_close(sockfd);
}


int he_connector1()
{
    in_port_t server_port;
    pthread_t server_thread;

    int rc = accept_async(&server_port, &server_thread, &after_accept, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "accept_async() failed\n");
        return -1;
    }

    rc = -1;
    zv_loop_t * loop = zv_loop_create(NULL);
    if (loop != NULL)
    {
        /* First 3 addresses: 192.0.2.1 (TEST-NET-1, unreachable) ports 1..3,
         * last one: loopback with the port to connect to.
         */
        struct addrinfo * addrs = make_test_addrs(
            4,
            (uint32_t) 0xC0000201, 1,
            (uint32_t) 0xC0000201, 2,
            (uint32_t) 0xC0000201, 3,
            (uint32_t) INADDR_LOOPBACK, server_port
        );

        if (addrs != NULL)
        {
            _zv_dns_resolver_addrs = addrs;

            zv_he_connector_t * he_connector = zv_he_connector_create(
                loop,
                "www.google.com",
                "http",
                SOCK_STREAM,
                &after_connect,
                &after_fail,
                NULL
            );

            if (he_connector != NULL)
            {
                zv_loop_run(loop);
                zv_he_connector_release(he_connector);
                rc = 0;
            }
            else
                fprintf(stderr, "zv_he_connector_create() failed\n");
        }
        else
            fprintf(stderr, "make_test_addrs() failed\n");

        zv_loop_destroy(loop);
    }
    else
        fprintf(stderr, "zv_loop_create() failed\n");

    pthread_join(server_thread, NULL);

    return rc;
}
