#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/os_time.h"
#include "helpers.h"

static const char SEND_DATA[] = "Hello, world.";
#define NUM_SEND_REQS (1000 * 1000)


static void channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    // no any data expected
    abort();
}


static void channel_close_handler(zv_channel_t * channel)
{
    printf(
        "Connection %s -> %s: closed\n",
        zv_channel_get_local_addr_str(channel),
        zv_channel_get_peer_addr_str(channel)
    );

    zv_loop_t * loop = zv_channel_get_loop(channel);
    zv_loop_stop(loop);
}


static void after_connect(zv_connector_t * connector, zv_socket_t sockfd)
{
    zv_loop_t * loop = zv_connector_get_loop(connector);
    zv_connector_destroy(connector);

    zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(1024 * 8, 16, 4);
    if (!buffer_cache)
        abort();

    zv_channel_t * channel = zv_channel_create(
        loop,
        buffer_cache,
        sockfd,
        &channel_input_handler,
        &channel_close_handler,
        NULL /*extra data*/
    );

    zv_buffer_cache_release(buffer_cache);

    if (channel)
    {
        printf(
            "Connection %s -> %s: established\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );

        zv_channel_start(channel);

        struct timespec ts_t1;
        clock_gettime(CLOCK_MONOTONIC, &ts_t1);

        char message[sizeof(struct timespec) + sizeof(SEND_DATA)];
        memcpy(message, &ts_t1, sizeof(struct timespec));
        memcpy(message + sizeof(struct timespec), SEND_DATA, sizeof(SEND_DATA));

        int count = NUM_SEND_REQS;
        for (; count>0; count--)
        {
            int rc = zv_channel_send(channel, message, sizeof(message));
            if (rc != 0)
            {
                fprintf(stderr, "zv_channel_send() failed\n");
                break;
            }
        }

        if (count == 0)
        {
            struct timespec ts_t2;
            clock_gettime(CLOCK_MONOTONIC, &ts_t2);

            long elapsed_time_ms = zv_elapsed_time_us(&ts_t1, &ts_t2) / 1000;
            printf(
                "> sent %d requests (%zu bytes) in %ld.%.3ld sec\n",
                NUM_SEND_REQS,
                sizeof(message) * NUM_SEND_REQS,
                elapsed_time_ms / 1000,
                elapsed_time_ms % 1000
            );
        }

        zv_channel_release(channel);
    }
}


static void server_thread_func(zv_socket_t accept_socket)
{
    size_t buffer_size = (32 * 1024);
    char * buffer = malloc(buffer_size);
    if (buffer == NULL)
    {
        fprintf(stderr, "malloc() failed\n");
        abort();
    }

    zv_socket_t sockfd = accept(accept_socket, NULL, 0);
    const size_t bytes_expected = ((sizeof(struct timespec) + sizeof(SEND_DATA)) * NUM_SEND_REQS);
    size_t bytes_received = 0;

    struct timespec ts_t1;

    ssize_t rc = recv(sockfd, buffer, buffer_size, 0);
    if (rc > 0)
    {
        memcpy(&ts_t1, buffer, sizeof(struct timespec));
        bytes_received = rc;
        while (bytes_received < bytes_expected)
        {
            ssize_t rc = recv(sockfd, buffer, buffer_size, 0);
            if (rc < 0)
            {
                fprintf(stderr, "recv() failed.\n");
                break;
            }
            bytes_received += rc;
        }
    }
    else
        fprintf(stderr, "recv() failed.\n");

    if (bytes_received < bytes_expected)
    {
        fprintf(
            stderr,
            "received %zu bytes instead of expected %zu\n",
            bytes_received,
            bytes_expected
        );
    }
    else
    {
        struct timespec ts_t4;
        clock_gettime(CLOCK_MONOTONIC, &ts_t4);

        long elapsed_time_ms = zv_elapsed_time_us(&ts_t1, &ts_t4) / 1000;
        printf(
            "> transmitted %zu bytes, (t4-t1) = %ld.%.3ld sec\n",
            bytes_expected,
            elapsed_time_ms / 1000,
            elapsed_time_ms % 1000
        );
    }

    zv_socket_close(sockfd);
    zv_socket_close(accept_socket);
}


int main(int argc, const char * argv[])
{
    in_port_t server_port;
    pthread_t server_thread;
    int rc;

#if defined(_WIN32)
    if (wsa_init() != 0)
        return -1;
#endif

    rc = start_raw_server(&server_port, &server_thread, &server_thread_func);
    if (rc == 0)
    {
        printf("server started at port %hu\n", server_port);

        zv_loop_t * loop = create_loop_and_connector(server_port, &after_connect);
        if (loop)
        {
            zv_loop_run(loop);
            zv_loop_destroy(loop);
        }

        pthread_join(server_thread, NULL);
    }
    else
        fprintf(stderr, "start_sock_server() failed\n");

#if defined(_WIN32)
    WSACleanup();
#endif

    return rc;
}
