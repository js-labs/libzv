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

#if !defined(ZV_OS_SOCKET_H)
#define ZV_OS_SOCKET_H

#include "zv/export.h"

#if defined(_WIN32)

#include <WinSock2.h>

typedef SOCKET zv_socket_t;
typedef int socklen_t;
typedef USHORT in_port_t;
typedef ULONG in_addr_t;

#if defined(_WIN64)
#define PRIsocket "lld"
typedef __int64 ssize_t;
#else
#define PRIsocket "ld"
typedef int ssize_t;
#endif

#elif defined(__linux__) || defined(__APPLE__)

#include <sys/socket.h>
#include <netinet/in.h>

typedef int zv_socket_t;

#define INVALID_SOCKET -1
#define PRIsocket "d"

#else
#error Not supported
#endif

ZV_EXPORT int zv_socket_close(zv_socket_t sockfd);

ZV_EXPORT const char * zv_sockaddr_ntop(
    const struct sockaddr * addr,
    socklen_t addrlen,
    char * buffer,
    size_t buffer_size
);

#if defined(_WIN32)
ZV_EXPORT int socketpair(int domain, int type, int protocol, SOCKET sv[2]);
#endif

#endif /* !defined(ZV_OS_SOCKET_H) */
