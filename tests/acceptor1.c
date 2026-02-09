/*
 * Copyright (C) 2022 Sergey Zubarev, info@js-labs.org
 *
 * This file is a part of tests for ZV framework.
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
#include "helpers.h"
#include <errno.h>
#include <string.h>

#if defined(_WIN32)
#include <ws2ipdef.h>
#endif

static void after_acceptor_stop(zv_acceptor_t * acceptor, void * arg)
{
    zv_loop_t * loop = zv_acceptor_get_loop(acceptor);
    zv_loop_stop(loop);
    zv_acceptor_destroy(acceptor);
}

static void accept_handler(zv_acceptor_t * acceptor, zv_socket_t sockfd)
{
    char local_addr_str[INET_ADDRSTRLEN];
    char peer_addr_str[INET_ADDRSTRLEN];
    int rc = get_sock_names(
        sockfd,
        local_addr_str, sizeof(local_addr_str),
        peer_addr_str, sizeof(peer_addr_str)
    );
    if (rc == 0)
        printf("connection accepted: %s -> %s\n", local_addr_str, peer_addr_str);
    else
        printf("get_sock_names(%" PRIsocket ") failed: %d\n", sockfd, errno);

    zv_socket_close(sockfd);
}

int acceptor1()
{
    pthread_t loop_thread;
    zv_loop_t * loop = start_loop(&loop_thread, NULL);
    if (!loop)
    {
        printf("start_loop() failed\n");
        return -1;
    }

    struct sockaddr_in addr = make_sockaddr_in(INADDR_ANY, 0);
    zv_acceptor_t * acceptor = zv_acceptor_create(
        loop,
        (struct sockaddr*) &addr,
        sizeof(addr),
        &accept_handler,
        NULL
    );

    int rc = 0;
    if (acceptor)
    {
        zv_acceptor_start(acceptor);
        in_port_t port = zv_acceptor_get_port(acceptor);
        printf("server started at port %hu\n", port);

        zv_socket_t sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd != INVALID_SOCKET)
        {
            addr = make_sockaddr_in(INADDR_LOOPBACK, port);
            rc = connect(sockfd, (const struct sockaddr*) &addr, sizeof(addr));
            if (rc != 0)
                printf("connect() failed");
            zv_socket_close(sockfd);
        }
        else
        {
            printf("socket() failed\n");
            rc = -1;
        }

        zv_acceptor_stop(acceptor, &after_acceptor_stop, NULL);
    }
    else
    {
        printf("zv_acceptor_create() failed\n");
        zv_loop_stop(loop);
        rc = -1;
    }

    pthread_join(loop_thread, NULL);

    return rc;
}
