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

#include "zv/connector.h"
#include "helpers.h"
#include <errno.h>
#include <string.h>
#include <stdint.h>

#if defined(_WIN32)
#include <ws2ipdef.h>
#endif

static void after_connect(zv_connector_t * connector, zv_socket_t sockfd)
{
    char local_addr_str[INET_ADDRSTRLEN];
    char peer_addr_str[INET_ADDRSTRLEN];
    int rc = get_sock_names(
        sockfd,
        local_addr_str, sizeof(local_addr_str),
        peer_addr_str, sizeof(peer_addr_str)
    );

    if (rc == 0)
        printf("connection established: %s -> %s\n", local_addr_str, peer_addr_str);
    else
        printf("get_sock_names(%" PRIsocket ") failed: %d\n", sockfd, errno);

    const char c = 42;
    send(sockfd, &c, sizeof(c), 0);
    zv_socket_close(sockfd);

    zv_loop_t * loop = zv_connector_get_loop(connector);
    zv_loop_stop(loop);

    zv_connector_destroy(connector);
}

static void after_fail(zv_connector_t * connector, int sockerr)
{
    printf("after_fail\n");
}

static void server_thread_func(zv_socket_t accept_socket)
{
    zv_socket_t sockfd = accept(accept_socket, NULL, 0);
    char c;
    int rc = recv(sockfd, &c, sizeof(c), 0);
    zv_socket_close(sockfd);
    zv_socket_close(accept_socket);
}

int connector1()
{
    in_port_t server_port;
    pthread_t server_thread;
    int rc = start_sock_server(&server_port, &server_thread, &server_thread_func);
    if (rc != 0)
        return rc;

    printf("server started at port %hu\n", server_port);

    zv_loop_t * loop = zv_loop_create(NULL);
    if (loop)
    {
        struct sockaddr_in addr = make_sockaddr_in(INADDR_LOOPBACK, server_port);
        zv_connector_t * connector = zv_connector_create(
            loop,
            (const struct sockaddr*) &addr,
            sizeof(addr),
            &after_connect,
            &after_fail,
            NULL /* extra data */
        );

        zv_connector_start(connector);
        zv_loop_run(loop);
        zv_loop_destroy(loop);
    }
    else
        printf("zv_loop_create() failed\n");

    pthread_join(server_thread, NULL);

    return 0;
}
