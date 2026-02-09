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

#include "zv/os_socket.h"
#include "misc.h"
#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif


int zv_socket_close(zv_socket_t sockfd)
{
#if defined(_WIN32)
    return closesocket(sockfd);
#else
    return close(sockfd);
#endif
}


const char * zv_inet_ntop(const struct sockaddr * addr, socklen_t addrlen, char * buffer, size_t buffer_size)
{
    const void * sin_addr;
    in_port_t port;
    if (addr->sa_family == AF_INET)
    {
        const struct sockaddr_in * addr_in = (const struct sockaddr_in*) addr;
        sin_addr = &(addr_in->sin_addr);
        port = addr_in->sin_port;
    }
    else if (addr->sa_family == AF_INET6)
    {
        const struct sockaddr_in6 * addr_in6 = (const struct sockaddr_in6*) addr;
        sin_addr = &(addr_in6->sin6_addr);
        port = addr_in6->sin6_port;
    }
    else
    {
        _zv_set_errno(EAFNOSUPPORT);
        return NULL;
    }
    inet_ntop(addr->sa_family, sin_addr, buffer, buffer_size);
    size_t len = strlen(buffer);
    if (len < buffer_size)
        snprintf(buffer+len, buffer_size-len, ":%hu", ntohs(port));
    return buffer;
}
