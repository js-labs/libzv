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

#include <stdio.h>
#include <string.h>
#include "zv/channel.h"
#include "zv/log.h"
#include "zv/loop.h"

#if defined(_WIN32)
#include <WinSock2.h>
#endif

#define ARRAY_SIZE(__array) (sizeof(__array)/sizeof(__array[0]))

#define ENABLE_LOGS

struct benchmark_s
{
    const char* name;
    int (*func)();
};

int benchmark_tcp_broadcast(int clients);
int benchmark_tcp_echo_latency();
int benchmark_tcp_ping_pongs(int pingers, int use_libzv);
int benchmark_tcp_recv_throughput();
int benchmark_thread_pool_latency();
int benchmark_thread_pool_throughput();
int benchmark_udp_echo_latency();

static int benchmark_tcp_broadcast_2() { return benchmark_tcp_broadcast(2); }
static int benchmark_tcp_broadcast_10() { return benchmark_tcp_broadcast(10); }
static int benchmark_tcp_ping_pongs_1() { return benchmark_tcp_ping_pongs(1, 1); }
static int benchmark_tcp_ping_pongs_10() { return benchmark_tcp_ping_pongs(10, 1); }
static int benchmark_tcp_ping_pongs_sts_1() { return benchmark_tcp_ping_pongs(1, 0); }
static int benchmark_tcp_ping_pongs_sts_10() { return benchmark_tcp_ping_pongs(10, 0); }

static struct benchmark_s benchmarks[] =
{
    { "tcp_broadcast",          &benchmark_tcp_broadcast_2 },
    { "tcp_broadcast_10",       &benchmark_tcp_broadcast_10 },
    { "tcp_echo_latency",       &benchmark_tcp_echo_latency },
    { "tcp_ping_pongs_1",       &benchmark_tcp_ping_pongs_1 },
    { "tcp_ping_pongs_10",      &benchmark_tcp_ping_pongs_10 },
    { "tcp_ping_pongs_sts_1",   &benchmark_tcp_ping_pongs_sts_1 },
    { "tcp_ping_pongs_sts_10",  &benchmark_tcp_ping_pongs_sts_10 },
    { "thread_pool_latency",    &benchmark_thread_pool_latency },
    { "thread_pool_throughput", &benchmark_thread_pool_throughput },
    { "udp_echo_latency",       &benchmark_udp_echo_latency }
};

static zv_log_backend_t * setup_logs(const char * benchmark_name)
{
    char log_file_name[128];
    snprintf(log_file_name, sizeof(log_file_name), "%s.log", benchmark_name);
    FILE * log_file = fopen(log_file_name, "wt");
    if (log_file == NULL)
    {
        fprintf(stderr, "fopen('%s') failed\n", log_file_name);
        return NULL;
    }
    else
        return zv_log_add_file_backend(log_file);
}

int main(int argc, const char * argv[])
{
    int rc;

#if defined(_WIN32)
    WORD version_requested = MAKEWORD(2, 2);
    WSADATA wsa_data;
    rc = WSAStartup(version_requested, &wsa_data);
    if (rc != 0)
    {
        fprintf(stderr, "WSAStartup() failed: %d\n", WSAGetLastError());
        return -1;
    }

    if ((HIBYTE(wsa_data.wVersion) != 2) && (LOBYTE(wsa_data.wVersion) != 2))
    {
        fprintf(
            stderr,
            "unsupported WSA version %hu.%hu\n",
            HIBYTE(wsa_data.wVersion),
            LOBYTE(wsa_data.wVersion)
        );
        return -1;
    }
#endif

#if defined(ENABLE_LOGS)
    zv_channel_set_log_level(zv_log_level_debug);
    zv_loop_set_log_level(zv_log_level_debug);
#endif

    if (argc > 1)
    {
        if (strcmp(argv[1], "--list") == 0)
        {
            for (size_t idx=0; idx<ARRAY_SIZE(benchmarks); idx++)
                printf("%s\n", benchmarks[idx].name);
            return 0;
        }

        size_t idx = 0;
        for (; idx<ARRAY_SIZE(benchmarks); idx++)
        {
            const struct benchmark_s * benchmark = &benchmarks[idx];
            if (strcmp(benchmark->name, argv[1]) == 0)
            {
#if defined(ENABLE_LOGS)
                zv_log_backend_t * log_backend = setup_logs(benchmark->name);
#endif

                rc = benchmark->func();

#if defined(ENABLE_LOGS)
                if (log_backend != NULL)
                {
                    FILE * file = zv_log_remove_file_backend(log_backend);
                    fclose(file);
                }
#endif

                return rc;
            }
        }

        if (idx == ARRAY_SIZE(benchmarks))
        {
            fprintf(stderr, "benchmark '%s' not found\n", argv[1]);
            return -1;
        }
    }
    else
    {
        for (size_t idx=0; idx<ARRAY_SIZE(benchmarks); idx++)
        {
            const struct benchmark_s * benchmark = &benchmarks[idx];
            benchmark->func();
        }
    }

#if defined(_WIN32)
    WSACleanup();
#endif

    return rc;
}
