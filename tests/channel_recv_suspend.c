#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/config.h"
#include "zv/os_time.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "helpers.h"

// Test reproduces channel suspension.
// It processes data slowly, causing the channel to stop reading
// from the socket and then resume reading.
// The test runs twice: 
// - The first time, the client closes the connection at the end.
// - The second time, the channel is closed on the server side while it is suspended.

// Size enough to trigger channel suspension 2 times
#define SEND_DATA_SIZE (80 * 1000)

struct acceptor_extra_data_s
{
    zv_buffer_cache_t * buffer_cache;
    void (*channel_input_handler)(zv_channel_t*, const void*, size_t);
};


static void after_acceptor_stop(zv_acceptor_t * acceptor, void * arg)
{
    struct acceptor_extra_data_s * extra_data = zv_acceptor_get_extra_data(acceptor);
    zv_buffer_cache_release(extra_data->buffer_cache);
    free(extra_data);
    zv_acceptor_destroy(acceptor);
}


static void test1_channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    // sleep some time letting channel to breach input queue high watermark
    usleep(50 * 1000);
    uintptr_t bytes_received = (uintptr_t) zv_channel_get_extra_data(channel);
    bytes_received += size;
    zv_channel_set_extra_data(channel, (void*) bytes_received);
    if (bytes_received == SEND_DATA_SIZE)
    {
        char c = 0;
        zv_channel_send(channel, &c, sizeof(c));
    }
}


static void test2_channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    // sleep some time letting channel to breach input queue high watermark
    usleep(50 * 1000);
    uintptr_t bytes_received = (uintptr_t) zv_channel_get_extra_data(channel);
    if (bytes_received == 0)
        zv_channel_stop(channel);
    bytes_received += size;
    zv_channel_set_extra_data(channel, (void*)bytes_received);
}


static void channel_close_handler(zv_channel_t * channel)
{
    uintptr_t bytes_received = (uintptr_t) zv_channel_get_extra_data(channel);
    printf(
        "connection %s -> %s closed, %lu bytes received\n",
        zv_channel_get_local_addr_str(channel),
        zv_channel_get_peer_addr_str(channel),
        (unsigned long) bytes_received
    );
    zv_loop_t * loop = zv_channel_get_loop(channel);
    zv_loop_stop(loop);
}


static void accept_handler(zv_acceptor_t * acceptor, zv_socket_t sockfd)
{
    zv_loop_t * loop = zv_acceptor_get_loop(acceptor);
    struct acceptor_extra_data_s * extra_data = zv_acceptor_get_extra_data(acceptor);

    zv_channel_t * channel = zv_channel_create(
        loop,
        extra_data->buffer_cache,
        sockfd,
        extra_data->channel_input_handler,
        &channel_close_handler,
        NULL /*extra data*/
    );

    if (channel)
    {
        printf(
            "connection accepted: %s -> %s\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );
        zv_channel_start(channel);
        zv_channel_release(channel);
        zv_acceptor_stop(acceptor, &after_acceptor_stop, NULL);
    }
    else
    {
        printf("zv_channel_create() failed\n");
        abort();
    }
}


static zv_acceptor_t * start_acceptor(
    zv_loop_t * loop,
    void (*channel_input_handler)(zv_channel_t*, const void*, size_t)
) {
    struct acceptor_extra_data_s * extra_data = malloc(sizeof(struct acceptor_extra_data_s));
    if (extra_data)
    {
        zv_buffer_cache_t* buffer_cache = zv_buffer_cache_create(1024 * 4, 16, 2);
        if (buffer_cache)
        {
            extra_data->buffer_cache = buffer_cache;
            extra_data->channel_input_handler = channel_input_handler;

            struct sockaddr_in addr = make_sockaddr_in(INADDR_ANY, 0);

            zv_acceptor_t * acceptor = zv_acceptor_create(
                loop,
                (const struct sockaddr*)&addr,
                sizeof(addr),
                &accept_handler,
                extra_data
            );

            if (acceptor)
            {
                zv_acceptor_start(acceptor);
                return acceptor;
            }
 
            printf("zv_acceptor() failed\n");
            zv_buffer_cache_release(buffer_cache);
        }
        else
            printf("zv_buffer_cache_create() failed\n");
        free(extra_data);
    }
    else
        printf("malloc() failed\n");
    return NULL;
}


static int run_test(
    void (*channel_input_handler)(zv_channel_t*, const void*, size_t)
) {
    struct zv_loop_config_s loop_config = *zv_loop_config_get_default();
    loop_config.channel.in_queue.low_watermark = (10 * 1024);
    loop_config.channel.in_queue.high_watermark = (32 * 1024);

    pthread_t server_thread;
    zv_loop_t * loop = start_loop(&server_thread, &loop_config);
    if (!loop)
        return -1;

    int rc = 0;
    zv_acceptor_t * acceptor = start_acceptor(loop, channel_input_handler);
    if (acceptor)
    {
        in_port_t server_port = zv_acceptor_get_port(acceptor);
        printf("server started at port %hu\n", server_port);

        zv_socket_t sockfd = sock_connect(INADDR_LOOPBACK, server_port);
        if (sockfd != INVALID_SOCKET)
        {
            int buffer_size = (4 * 1024);
            char * buffer = malloc(buffer_size);
            if (buffer)
            {
                memset(buffer, 42, buffer_size);

                size_t bytes_remaining = SEND_DATA_SIZE;
                while (bytes_remaining > 0)
                {
                    size_t bytes_to_send = ((bytes_remaining > buffer_size) ? buffer_size : bytes_remaining);
                    ssize_t bytes_sent = send(sockfd, (const char*)buffer, bytes_to_send, 0);
                    if (bytes_sent < 0)
                    {
                        printf("send() failed\n");
                        break;
                    }
                    bytes_remaining -= bytes_sent;
                }

                // waiting for reply from the server
                recv(sockfd, buffer, 1, 0);

                free(buffer);
            }
            else
            {
                printf("malloc() failed\n");
                rc = -1;
            }
            zv_socket_close(sockfd);
        }
        else
        {
            zv_acceptor_stop(acceptor, &after_acceptor_stop, NULL);
            rc = -1;
        }
    }
    else
    {
        printf("start_acceptor() failed\n");
        zv_loop_stop(loop);
        rc = -1;
    }

    pthread_join(server_thread, NULL);

    return 0;
}


int channel_recv_suspend()
{
    int rc;
    rc = run_test(&test1_channel_input_handler);
    if (rc != 0)
        return rc;
    rc = run_test(&test2_channel_input_handler);
    return rc;
}
