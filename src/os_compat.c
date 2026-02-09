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

#include "zv/os_compat.h"
#include <stdio.h>

#if defined(_WIN32)
#include <WS2tcpip.h>
typedef SOCKET zv_socket_t;
#elif defined(__linux__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#endif

int zv_pipe(zv_socket_t pipefd[2])
{
#if defined(_WIN32)
    SOCKET listen_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET)
        return -1;

    struct sockaddr_in sockaddr;
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    sockaddr.sin_port = 0;

    int rc = bind(listen_socket, (struct sockaddr*)&sockaddr, sizeof(sockaddr));
    if (rc == 0)
    {
        int namelen = sizeof(sockaddr);
        rc = getsockname(listen_socket, (struct sockaddr*)&sockaddr, &namelen);
        if (rc == 0)
        {
            rc = listen(listen_socket, 1);
            if (rc == 0)
            {
                SOCKET pipe1 = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (pipe1 != INVALID_SOCKET)
                {
                    u_long mode = 1;
                    rc = ioctlsocket(pipe1, FIONBIO, &mode);
                    if (rc == 0)
                    {
                        sockaddr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);
                        rc = connect(pipe1, (struct sockaddr*)&sockaddr, sizeof(sockaddr));
                        if ((rc == 0) || ((rc = WSAGetLastError()) == WSAEWOULDBLOCK))
                        {
                            SOCKET pipe0 = accept(listen_socket, NULL, NULL);
                            if (pipe0 != INVALID_SOCKET)
                            {
                                rc = ioctlsocket(pipe1, FIONBIO, &mode);
                                if (rc == 0)
                                {
                                    closesocket(listen_socket);

                                    WSAPOLLFD pollfd;
                                    pollfd.fd = pipe1;
                                    pollfd.events = POLLOUT;
                                    pollfd.revents = 0;

                                    rc = WSAPoll(&pollfd, 1, INFINITE);
                                    if ((rc == 1) && (pollfd.revents & POLLOUT))
                                    {
                                        pipefd[0] = pipe0;
                                        pipefd[1] = pipe1;
                                        return 0;
                                    }

                                    rc = WSAGetLastError();
                                }
                                else
                                    rc = WSAGetLastError();
                            }
                            else
                                rc = WSAGetLastError();
                        }
                    }
                    else
                        rc = WSAGetLastError();

                    closesocket(pipe1);
                }
                else
                    rc = WSAGetLastError();
            }
            else
                rc = WSAGetLastError();
        }
        else
            rc = WSAGetLastError();
    }
    else
        rc = WSAGetLastError();

    closesocket(listen_socket);
    WSASetLastError(rc);

    return -1;
#else
    return pipe(pipefd);
#endif
}

