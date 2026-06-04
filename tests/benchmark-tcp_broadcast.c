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

 /*
  * TCP Broadcast Performance Benchmark
  *
  *   Measures the throughput of broadcasting TCP messages from a
  *   single publisher to multiple concurrent subscribers via a
  *   central server.
  *
  * Test topology:
  *   - One broadcast server
  *   - N clients connect to the server
  *   - One of the clients acts as the publisher; the remaining (N-1)
  *     act as subscribers
  *
  * Measurement:
  *   Elapsed time = timestamp of the last subscriber completion
  *                - timestamp when the first message was enqueued by
  *                  the sender thread.
  *   The result is reported as total elapsed time in seconds for
  *   broadcasting <message_count> messages of <message_size> bytes each.
  */

#include "zv/acceptor.h"
#include "zv/array.h"
#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/loop.h"
#include "zv/os_time.h"
#include "zv/stream_defragger.h"

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tcp_broadcast_server.h"
#include "helpers.h"

struct client_s
{
    struct test_s * test;
    zv_buffer_cache_t * buffer_cache;
    struct zv_stream_defragger_s stream_defragger;
    size_t received_messages;
    int sender;
    pthread_t sender_thread;
};


struct test_s
{
    size_t clients_total;
    size_t message_size;
    size_t message_count;
    struct timespec start_time;
    size_t clients_completed;
    long test_time_us;
    struct client_s * clients;
    _Atomic size_t clients_disconnected;

    pthread_mutex_t mutex;
    zv_channel_t ** channels;
};


static int test_init(
    struct test_s * test,
    size_t clients_total,
    size_t message_size,
    size_t message_count
) {
    test->clients_total = clients_total;
    test->message_size = message_size;
    test->message_count = message_count;
    test->clients_completed = 0;
    test->test_time_us = 0;

    test->clients = malloc(sizeof(struct client_s) * clients_total);
    if (test->clients != NULL)
    {
        test->clients_disconnected = 0;
        int rc = pthread_mutex_init(&test->mutex, NULL);
        if (rc == 0)
        {
            test->channels = NULL;
            return 0;
        }
        else
            fprintf(stderr, "pthread_mutex_init() failed");
        free(test->clients);
    }
    else
        fprintf(stderr, "malloc() failed\n");

    return -1;
}


static void test_deinit(struct test_s * test)
{
    pthread_mutex_destroy(&test->mutex);
    free(test->clients);
}


static void test_add_channel(struct test_s * test, zv_channel_t * channel)
{
    pthread_mutex_lock(&test->mutex);
    *zv_array_append(test->channels) = zv_channel_retain(channel);
    pthread_mutex_unlock(&test->mutex);
}


static void test_stop_channels(struct test_s * test)
{
    pthread_mutex_lock(&test->mutex);
    zv_channel_t ** channels = test->channels;
    test->channels = NULL;
    pthread_mutex_unlock(&test->mutex);

    size_t count = zv_array_get_size(channels);
    for (size_t idx=0; idx<count; idx++)
    {
        zv_channel_t * channel = channels[idx];
        zv_channel_stop(channel);
        zv_channel_release(channel);
    }
    zv_array_destroy(channels);
}


static int client_init(struct client_s * client, struct test_s * test)
{
    zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(1024*32, 8, 8);
    if (buffer_cache == NULL)
    {
        fprintf(stderr, "zv_buffer_cache_create() failed\n");
        return -1;
    }
    client->test = test;
    client->buffer_cache = buffer_cache;
    zv_stream_defragger_init(&client->stream_defragger);
    client->received_messages = 0;
    client->sender = 0;
    return 0;
}


static void client_deinit(struct client_s * client)
{
    zv_stream_defragger_deinit(&client->stream_defragger);
    zv_buffer_cache_release(client->buffer_cache);
}


static size_t message_size_cb(const void * data)
{
    uint16_t message_size;
    memcpy(&message_size, data, sizeof(uint16_t));
    return message_size;
}


struct sender_args_s
{
    struct test_s * test;
    zv_channel_t * channel;
};


static void * sender_thread_func(void * _arg)
{
    struct sender_args_s * arg = _arg;
    struct test_s * test = arg->test;
    zv_channel_t * channel = arg->channel;
    free(_arg);

    size_t message_min_size = sizeof(struct msg_test_message_s);
    size_t message_size = test->message_size;
    if (message_size < message_min_size)
        message_size = message_min_size;
    if (message_size > UINT16_MAX)
        message_size = UINT16_MAX;

    void * buffer = malloc(message_size);
    if (buffer == NULL)
        abort();

    struct msg_test_message_s * test_message = buffer;
    test_message->size = (uint16_t) message_size;
    test_message->id = MSG_TEST_MESSAGE;
    test_message->message_count = test->message_count;

    char * ptr = ((char*)buffer) + sizeof(struct msg_test_message_s);
    for (size_t idx=0; idx<(message_size - message_min_size); idx++)
        *(ptr++) = (char) idx;

    clock_gettime(CLOCK_MONOTONIC, &test->start_time);

    for (size_t count = test->message_count; count > 0; count--)
        zv_channel_send(channel, buffer, message_size);

    zv_channel_release(channel);
    return NULL;
}


static void channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    /*
    printf(
        "%s -> %s: received %zu bytes\n",
        zv_channel_get_local_addr_str(channel),
        zv_channel_get_peer_addr_str(channel),
        size
    );
    */

    struct client_s * client = zv_channel_get_extra_data(channel);
    struct zv_stream_defragger_state_s state = { data, size };
    for (;;)
    {
        size_t message_size;
        const void * message = zv_stream_defragger_get_next(
            &client->stream_defragger,
            sizeof(uint16_t), // header size
            &message_size_cb,
            &state,
            &message_size
        );

        if (message == NULL)
            break;

        const uint16_t message_id = ((const uint16_t*)message)[1];
        if (message_id == MSG_CONNECTED_CLIENTS)
        {
            assert(message_size == sizeof(struct msg_connected_clients_s));
            assert(client->received_messages == 0);
            struct test_s * test = client->test;
            struct msg_connected_clients_s connected_clients;
            memcpy(&connected_clients, message, sizeof(connected_clients));

            if (connected_clients.count == (test->clients_total - 1))
            {
                printf(
                    "%zu clients connected to the server, starting test...\n",
                    test->clients_total
                );
                client->sender = 1;

                struct sender_args_s * sender_args = malloc(sizeof(struct sender_args_s));
                if (sender_args == NULL)
                    abort();
                sender_args->test = test;
                sender_args->channel = zv_channel_retain(channel);

                int rc = pthread_create(&client->sender_thread, NULL, sender_thread_func, sender_args);
                if (rc != 0)
                    abort();
            }
        }
        else if (message_id == MSG_TEST_MESSAGE)
        {
            size_t received_messages = ++client->received_messages;
            size_t message_count;
            memcpy(
                &message_count,
                ((char*)message) + offsetof(struct msg_test_message_s, message_count),
                sizeof(message_count)
            );

            if (received_messages == message_count)
            {
                struct msg_client_completed_s client_completed;
                client_completed.size = sizeof(client_completed);
                client_completed.id = MSG_CLIENT_COMPLETED;
                clock_gettime(CLOCK_MONOTONIC, &client_completed.ts);

                printf(
                    "%s -> %s: received %zu messages\n",
                    zv_channel_get_local_addr_str(channel),
                    zv_channel_get_peer_addr_str(channel),
                    message_count
                );

                zv_channel_send(channel, &client_completed, sizeof(client_completed));
            }
        }
        else if (message_id == MSG_CLIENT_COMPLETED)
        {
            if (client->sender)
            {
                struct test_s * test = client->test;
                struct msg_client_completed_s client_completed;
                memcpy(&client_completed, message, sizeof(client_completed));
                long elapsed_time_us = zv_elapsed_time_us(&test->start_time, &client_completed.ts);
                if (elapsed_time_us > test->test_time_us)
                    test->test_time_us = elapsed_time_us;

                size_t clients_completed = ++test->clients_completed;
                if (clients_completed == (test->clients_total - 1))
                {
                    long test_time_ms = (test->test_time_us / 1000);
                    printf(
                        "Broadcasted %zu messages (%zu bytes each) at %ld.%03ld sec\n",
                        test->message_count,
                        test->message_size,
                        (test_time_ms / 1000),
                        (test_time_ms % 1000)
                    );

                    pthread_join(client->sender_thread, NULL);
                    test_stop_channels(test);
                }
            }
        }
        else
            abort();
    }
}


static void channel_close_handler(zv_channel_t * channel)
{
    printf("%s -> %s: connection closed\n",
        zv_channel_get_local_addr_str(channel),
        zv_channel_get_peer_addr_str(channel)
    );
    struct client_s * client = zv_channel_get_extra_data(channel);
    struct test_s * test = client->test;
    size_t clients_disconnected = atomic_fetch_add(&test->clients_disconnected, 1) + 1;
    if (clients_disconnected == test->clients_total)
    {
        zv_loop_t * loop = zv_channel_get_loop(channel);
        zv_loop_stop(loop);
    }
}


static void after_connect(zv_connector_t * connector, zv_socket_t sockfd)
{
    zv_loop_t * loop = zv_connector_get_loop(connector);
    struct client_s * client = zv_connector_get_extra_data(connector);
    zv_connector_destroy(connector);

    zv_channel_t * channel = zv_channel_create(
        loop,
        client->buffer_cache,
        sockfd,
        &channel_input_handler,
        &channel_close_handler,
        client
    );

    if (channel != NULL)
    {
        printf(
            "%s -> %s: connection established\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );
        test_add_channel(client->test, channel);
        zv_channel_start(channel);
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
    fprintf(stderr, "after_connection_failure\n");
    abort();
}


static void run_clients(
    in_port_t server_port,
    size_t client_count,
    size_t message_size,
    size_t message_count
) {
    zv_loop_t * client_loop = zv_loop_create(NULL);
    if (client_loop == NULL)
    {
        fprintf(stderr, "zv_loop_create() failed\n");
        return;
    }

    struct test_s test;
    int rc = test_init(&test, client_count, message_size, message_count);
    if (rc == 0)
    {
        struct client_s * clients = test.clients;
        struct sockaddr_in addr = make_sockaddr_in(INADDR_LOOPBACK, server_port);
        size_t remaining_clients = client_count;
        size_t idx = 0;
        for (; idx<client_count; idx++)
        {
            int rc = client_init(&clients[idx], &test);
            if (rc != 0)
                break;

            zv_connector_t * connector = zv_connector_create(
                client_loop,
                (const struct sockaddr*) &addr,
                sizeof(addr),
                &after_connect,
                &after_connection_failure,
                &clients[idx]/* extra data */
            );

            if (connector != NULL)
                zv_connector_start(connector);
            else
                printf("zv_connector_create() failed\n");
        }

        if (idx == client_count)
            zv_loop_run(client_loop);

        size_t initialized_clients = idx;
        for (idx=0; idx<initialized_clients; idx++)
            client_deinit(&clients[idx]);

        test_deinit(&test);
    }
    else
        fprintf(stderr, "test_init() failed\n");

    zv_loop_destroy(client_loop);
}


int benchmark_tcp_broadcast(int clients)
{
    int rc;
    in_port_t server_port = 0;
    tcp_broadcast_server_t * broadcast_server = tcp_broadcast_server_create(NULL, &server_port);
    if (broadcast_server != NULL)
    {
        run_clients(
            server_port,
            clients,
            500, // message size
            1000000  // message count
        );
        tcp_broadcast_server_stop_and_destroy(broadcast_server);
        rc = 0;
    }
    else
    {
        fprintf(stderr, "broadcast_server() failed\n");
        rc = -1;
    }
    return rc;
}
