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

#include "zv/os_socket.h"
#include "zv/os_thread.h"
#include "zv/os_time.h"
#include "udp_echo_server.h"
#include "helpers.h"
#include "histogram.h"
#include <stdio.h>
#include <stdlib.h>

#define WARMUP_PACKETS 100
#define TEST_PACKETS 1000

static int send_packets_i(
    zv_socket_t sockfd,
    const struct sockaddr_in * addr,
    size_t count,
    struct timespec * tss
) {
    const char message[] = { 0, 1, 2, 3 };

    for (size_t idx=0; idx<count; idx++)
    {
        clock_gettime(CLOCK_MONOTONIC, &tss[idx]);

        int bytes_sent = sendto(
            sockfd,
            (void*) &message,
            sizeof(message),
            0,
            (const struct sockaddr*) addr,
            sizeof(struct sockaddr_in)
        );

        if (bytes_sent > 0)
        {
            char buffer[1600];
            struct sockaddr_in peer_addr;
            socklen_t peer_addrlen = sizeof(peer_addr);
            int bytes_received = recvfrom(
                sockfd,
                buffer,
                sizeof(buffer),
                0,
                (struct sockaddr*)&peer_addr,
                &peer_addrlen
            );

            if (bytes_received <= 0)
            {
                fprintf(stderr, "recvfrom() failed\n");
                return -1;
            }
        }
        else
        {
            fprintf(stderr, "sendto() failed\n");
            return -1;
        }
    }

    return 0;
}

static int run_benchmark(in_port_t server_port)
{
    zv_socket_t sockfd = socket(PF_INET, SOCK_DGRAM, 0);
    if (sockfd == INVALID_SOCKET)
    {
        fprintf(stderr, "socket() failed\n");
        return -1;
    }

    int rc;
    int max_packets = (TEST_PACKETS > WARMUP_PACKETS) ? TEST_PACKETS : WARMUP_PACKETS;
    struct timespec * tss = malloc(sizeof(struct timespec) * (max_packets + 1));
    if (tss != NULL)
    {
        struct sockaddr_in addr = make_sockaddr_in(INADDR_LOOPBACK, server_port);
        rc = send_packets_i(sockfd, &addr, WARMUP_PACKETS, tss);
        if (rc == 0)
        {
            rc = send_packets_i(sockfd, &addr, TEST_PACKETS, tss);
            if (rc == 0)
            {
                clock_gettime(CLOCK_MONOTONIC, &tss[TEST_PACKETS]);
                long stats[TEST_PACKETS];
                for (size_t idx = 0; idx < TEST_PACKETS; idx++)
                    stats[idx] = zv_elapsed_time_us(&tss[idx], &tss[idx + 1]);
                print_histogram(TEST_PACKETS, stats);
            }
        }
    }
    else
    {
        fprintf(stderr, "malloc() failed\n");
        rc = -1;
    }

#if defined(_WIN32)
    closesocket(sockfd);
#else
    close(sockfd);
#endif

    return rc;
}


int benchmark_udp_echo_latency()
{
    in_port_t server_port;
    udp_echo_server_t * server = udp_echo_server_create(NULL, &server_port);
    if (server == NULL)
    {
        fprintf(stderr, "udp_echo_server_create() failed\n");
        return -1;
    }

    run_benchmark(server_port);

    udp_echo_server_stop_and_destroy(server);
    return 0;
}
