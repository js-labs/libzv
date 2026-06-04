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
#include <errno.h>
#include <stdio.h>
#include "helpers.h"

#if defined(_WIN32)
#include <ws2ipdef.h>
#endif

static void after_connect(zv_connector_t * connector, zv_socket_t sockfd)
{
    abort(); // should not happen
}

static void after_fail(zv_connector_t * connector, int sockerr)
{
    socklen_t addrlen;
    const struct sockaddr * addr = zv_connector_get_addr(connector, &addrlen);

    char buffer[INET_ADDRSTRLEN];
    zv_inet_ntop(addr, addrlen, buffer, sizeof(buffer));
    printf("failed to connect to %s (expected)\n", buffer);

    zv_loop_t * loop = zv_connector_get_loop(connector);
    zv_loop_stop(loop);
    zv_connector_destroy(connector);
}

int connector3()
{
    zv_loop_t * loop = zv_loop_create(NULL);

    // hope no one responses at port 666 on 8.8.8.8
    const struct sockaddr_in addr = make_sockaddr_in(0x08080808, 666);
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

    return 0;
}
