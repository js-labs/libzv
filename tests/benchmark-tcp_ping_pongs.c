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

#include "zv/atomic.h"
#include "zv/acceptor.h"
#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/loop.h"
#include "zv/os_time.h"
#include <stdint.h>
#include <string.h>

#include "tcp_echo_server.h"
#include "helpers.h"

#if !defined(_WIN32)
#include <netinet/tcp.h>
#endif

#define TIME_MS (3*1000)

static const char PING[] = "PING\n";
#define PING_SIZE (sizeof(PING)-1)

struct pinger_s
{
    zv_buffer_cache_t * buffer_cache;
    struct timeval start_time;
    unsigned int state;
    size_t pongs;
    size_t * remaining_pingers;
};


static int pinger_init(struct pinger_s * pinger, size_t * remaining_pingers)
{
    zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(1024*8, 8, 2);
    if (buffer_cache != NULL)
    {
        pinger->buffer_cache = buffer_cache;
        pinger->state = 0;
        pinger->pongs = 0;
        pinger->remaining_pingers = remaining_pingers;
        return 0;
    }
    return -1;
}


static void pinger_deinit(struct pinger_s * pinger)
{
    zv_buffer_cache_release(pinger->buffer_cache);
}


static void channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    struct pinger_s * pinger = zv_channel_get_extra_data(channel);
    for (size_t i=0; i<size; i++)
    {
        pinger->state = (pinger->state + 1) % PING_SIZE;
        if (pinger->state == 0)
        {
            pinger->pongs++;
            struct timeval now;
            gettimeofday(&now, NULL);
            uint32_t elapsed_time_ms = timeval_elapsed_time(&pinger->start_time, &now) / 1000;
            if (elapsed_time_ms > TIME_MS)
                zv_channel_stop(channel);
            else
                zv_channel_send(channel, PING, PING_SIZE);
        }
    }
}


static void channel_close_handler(zv_channel_t * channel)
{
    struct pinger_s * pinger = zv_channel_get_extra_data(channel);
    size_t remaining_pingers = zv_atomic_sub_fetch(pinger->remaining_pingers, 1);
    /*
    printf("%s -> %s: connection closed, remaining_pingers=%zu\n",
        zv_channel_get_local_addr_str(channel),
        zv_channel_get_peer_addr_str(channel),
        remaining_pingers
    );
    */
    if (remaining_pingers == 0)
    {
        zv_loop_t * loop = zv_channel_get_loop(channel);
        zv_loop_stop(loop);
    }
}


static void after_connect(zv_connector_t * connector, zv_socket_t sockfd)
{
    zv_loop_t * loop = zv_connector_get_loop(connector);
    struct pinger_s * pinger = zv_connector_get_extra_data(connector);
    zv_connector_destroy(connector);

    zv_channel_t * channel = zv_channel_create(
        loop,
        pinger->buffer_cache,
        sockfd,
        &channel_input_handler,
        &channel_close_handler,
        pinger
    );

    if (channel)
    {
        /*
        printf(
            "%s -> %s: connection established\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );
        */
        gettimeofday(&pinger->start_time, NULL);
        zv_channel_start(channel);

        zv_channel_send(channel, &PING, PING_SIZE);
        zv_channel_release(channel);
    }
    else
    {
        // something really unexpected
        zv_socket_close(sockfd);
        fprintf(stderr, "zv_channel_create() failed\n");
    }
}


static void after_connection_failure(zv_connector_t * connector, int err)
{
    printf("after_connection_failure\n");
    abort();
}


static void run_pingers(in_port_t server_port, int count)
{
    zv_loop_t * client_loop = zv_loop_create(NULL);
    if (client_loop == NULL)
    {
        printf("zv_loop_create() failed\n");
        return;
    }

    struct pinger_s * pingers = malloc(count * sizeof(struct pinger_s));
    if (pingers != NULL)
    {
        struct sockaddr_in addr = make_sockaddr_in(INADDR_LOOPBACK, server_port);
        size_t remaining_pingers = count;
        for (int idx=0; idx<count; idx++)
        {
            if (pinger_init(&pingers[idx], &remaining_pingers) == 0)
            {
                zv_connector_t * connector = zv_connector_create(
                    client_loop,
                    (const struct sockaddr*) &addr,
                    sizeof(addr),
                    &after_connect,
                    &after_connection_failure,
                    &pingers[idx]/* extra data */
                );

                if (connector != NULL)
                    zv_connector_start(connector);
                else
                {
                    printf("zv_connector_create() failed\n");
                    abort();
                }
            }
        }

        zv_loop_run(client_loop);

        if (count == 1)
            printf("ping_pongs: %zu roundtrips/s\n", (1000 * pingers[0].pongs) / TIME_MS);
        else
        {
            size_t sum = 0;
            for (int idx=0; idx<count; idx++)
                sum += pingers[idx].pongs;
            printf("ping_pongs_%d: average %zu roundtrips/s\n", count, (1000 * sum) / TIME_MS / count);
        }

        for (int idx=0; idx<count; idx++)
            pinger_deinit(&pingers[idx]);
        free(pingers);
    }

    zv_loop_destroy(client_loop);
}


int benchmark_tcp_ping_pong(int pingers)
{
    int rc;

    /*
    FILE * log_file = fopen("logs", "wt");
    zv_log_backend_t * log_backend = zv_log_add_file_backend(log_file);
    zv_channel_set_log_level(zv_log_level_debug);
    zv_loop_set_log_level(zv_log_level_debug);
    */

    struct zv_loop_config_s loop_config = *zv_loop_config_get_default();
    in_port_t server_port;
    tcp_echo_server_t * echo_server = tcp_echo_server_create(&loop_config, &server_port);

    if (echo_server != NULL)
    {
        run_pingers(server_port, 10);
        tcp_echo_server_stop_and_destroy(echo_server);
        rc = 0;
    }
    else
    {
        fprintf(stderr, "echo_server() failed\n");
        rc = -1;
    }

    /*
    zv_log_remove_file_backend(log_backend);
    fclose(log_file);
    */

    return rc;
}
