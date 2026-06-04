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

#include "tcp_echo_server.h"
#include "helpers.h"
#include "histogram.h"
#include "zv/os_time.h"
#include <stdio.h>
#include <stdlib.h>

#if !defined(_WIN32)
#include <netinet/tcp.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#define WARMUP_COUNT 100
#define COUNT 1000


static int send_warmup_msgs(zv_socket_t sockfd, int count)
{
    for (int idx=0; idx<count; idx++)
    {
        uint32_t msg = idx;
#if defined(_WIN32)
        int rc = send(sockfd, (const char*) &msg, sizeof(msg), 0);
#else
        ssize_t rc = send(sockfd, &msg, sizeof(msg), 0);
#endif
        if (rc != sizeof(msg))
        {
            printf("failed to send message to server\n");
            return -1;
        }
        uint32_t reply;
        rc = recv(sockfd, (char*) &reply, sizeof(reply), 0);
        if (rc != sizeof(reply))
        {
            printf("failed to receive reply from server");
            return -1;
        }
        if (reply != msg)
        {
            printf("unexpected reply [%u] received instead of [%u]\n", reply, msg);
            return -1;
        }
    }
    return 0;
}


static int send_test_msgs(zv_socket_t sockfd, int count, long * response_times)
{
    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    for (int idx=0; idx<count; idx++)
    {
        uint32_t msg = idx;
#if defined(_WIN32)
        int rc = send(sockfd, (const char*) &msg, sizeof(msg), 0);
#else
        ssize_t rc = send(sockfd, &msg, sizeof(msg), 0);
#endif
        if (rc != sizeof(msg))
        {
            printf("failed to send message to server\n");
            return -1;
        }
        uint32_t reply;
        rc = recv(sockfd, (char*) &reply, sizeof(reply), 0);
        if (rc != sizeof(reply))
        {
            printf("failed to receive reply from server");
            return -1;
        }
        if (reply != msg)
        {
            printf("unexpected reply [%u] received instead of [%u]\n", reply, msg);
            return -1;
        }

        struct timespec ts_end;
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        response_times[idx] = zv_elapsed_time_us(&ts_start, &ts_end);
        ts_start = ts_end;
    }
    return 0;
}


static void run_benchmark(enum zv_loop_type_e loop_type)
{
    struct zv_loop_config_s loop_config = *zv_loop_config_get_default();
    loop_config.loop_type = loop_type;

    in_port_t server_port;
    tcp_echo_server_t * echo_server = tcp_echo_server_create(&loop_config, &server_port);
    if (echo_server == NULL)
    {
        printf("echo_server_create() failed\n");
        return;
    }

    long * response_times = NULL;
    struct sockaddr_in addr = make_sockaddr_in(INADDR_LOOPBACK, server_port);
    zv_socket_t sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd != INVALID_SOCKET)
    {
        int rc = connect(sockfd, (struct sockaddr*) &addr, sizeof(addr));
        if (rc == 0)
        {
        #if defined(_WIN32)
            DWORD nodelay = 1;
            rc = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
        #else
            int nodelay = 1;
            rc = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        #endif
            if (rc == 0)
            {
                response_times = malloc(sizeof(long) * COUNT);
                if (response_times != NULL)
                {
                    rc = send_warmup_msgs(sockfd, WARMUP_COUNT);
                    if (rc == 0)
                        send_test_msgs(sockfd, COUNT, response_times);
                    if (rc != 0)
                    {
                        free(response_times);
                        response_times = NULL;
                    }
                }
                else
                    fprintf(stderr, "malloc() failed\n");
            }
            else
                fprintf(stderr, "setsockopt() failed\n");
        }
        else
            fprintf(stderr, "connect() failed\n");

        zv_socket_close(sockfd);
    }
    else
        fprintf(stderr, "socket() failed\n");

    if (response_times != NULL)
    {
        print_histogram(COUNT, response_times);
        free(response_times);
    }

    tcp_echo_server_stop_and_destroy(echo_server);
}


int benchmark_tcp_echo_latency()
{
    enum zv_loop_type_e loop_types[] = {
        zv_loop_type_select
        /*
#if defined(__linux__) || defined(_WIN32)
        , zv_loop_type_poll
#endif
        */
    };

    // zv_log_add_file_backend(stdout);
    // zv_loop_set_log_level(zv_log_level_debug);

    for (int idx=0; idx<sizeof(loop_types)/sizeof(loop_types[0]); idx++)
    {
        enum zv_loop_type_e loop_type = loop_types[idx];
        run_benchmark(loop_type);
    }

    return 0;
}
