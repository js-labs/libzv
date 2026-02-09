#include <assert.h>
#include <inttypes.h>
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

        struct timespec ts_start;
        clock_gettime(CLOCK_MONOTONIC, &ts_start);

        for (int count=NUM_SEND_REQS; count>0; count--)
        {
            int rc = zv_channel_send(channel, SEND_DATA, sizeof(SEND_DATA));
            if (rc != 0)
            {
                printf("zv_channel_send() failed\n");
                break;
            }
        }

        struct timespec ts_end;
        clock_gettime(CLOCK_MONOTONIC, &ts_end);

        long elapsed_time_ms = zv_elapsed_time_us(&ts_start, &ts_end) / 1000;
        printf(
            "# %d write requests in %ld.%.3ld sec\n",
            NUM_SEND_REQS,
            elapsed_time_ms / 1000,
            elapsed_time_ms % 1000
        );

        zv_channel_release(channel);
    }
}


static void server_thread_func(zv_socket_t accept_socket)
{
    zv_socket_t sockfd = accept(accept_socket, NULL, 0);
    char buffer[1024*32];
    size_t bytes_received = 0;
    const size_t bytes_expected = (sizeof(SEND_DATA) * NUM_SEND_REQS);

    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    while (bytes_received < bytes_expected)
    {
        ssize_t rc = recv(sockfd, buffer, sizeof(buffer), 0);
        if (rc < 0)
        {
            printf("recv() failed.\n");
            break;
        }
        bytes_received += rc;
    }

    struct timespec ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_end);

    long elapsed_time_ms = zv_elapsed_time_us(&ts_start, &ts_end) / 1000;
    printf(
        "# received %zu bytes in %ld.%.3ld sec\n",
        bytes_expected,
        elapsed_time_ms / 1000,
        elapsed_time_ms % 1000
    );

    zv_socket_close(sockfd);
    zv_socket_close(accept_socket);
}


int main(int argc, const char * argv[])
{
    in_port_t server_port;
    pthread_t server_thread;
    int rc = start_sock_server(&server_port, &server_thread, &server_thread_func);
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
    return rc;
}
