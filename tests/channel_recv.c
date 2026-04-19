#include "zv/buffer.h"
#include "zv/channel.h"
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "helpers.h"

static void after_acceptor_stop(zv_acceptor_t * acceptor, void * arg)
{
    zv_buffer_cache_t * buffer_cache = zv_acceptor_get_extra_data(acceptor);
    zv_buffer_cache_release(buffer_cache);
    zv_acceptor_destroy(acceptor);
}


static void channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    uintptr_t bytes_received = (uintptr_t) zv_channel_get_extra_data(channel);
    bytes_received += size;
    zv_channel_set_extra_data(channel, (void*) bytes_received);
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
    zv_buffer_cache_t * buffer_cache = zv_acceptor_get_extra_data(acceptor);

    zv_channel_t * channel = zv_channel_create(
        loop,
        buffer_cache,
        sockfd,
        &channel_input_handler,
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


int channel_recv()
{
    pthread_t server_thread;
    zv_loop_t * loop = start_loop(&server_thread, NULL);
    if (!loop)
        return -1;

    int rc = 0;
    zv_acceptor_t * acceptor = start_acceptor_with_buffer_cache(loop, &accept_handler);
    if (acceptor)
    {
        in_port_t server_port = zv_acceptor_get_port(acceptor);
        printf("server started at port %hu\n", server_port);

        zv_socket_t sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd != INVALID_SOCKET)
        {
            struct sockaddr_in addr = make_sockaddr_in(INADDR_LOOPBACK, server_port);
            rc = connect(sockfd, (const struct sockaddr*) &addr, sizeof(addr));
            if (rc == 0)
            {
                int msg_count = (1024 * 32);
                int buffer_msg_count = (1024 * 4);
                int buffer_size = (sizeof(uint32_t) * buffer_msg_count);
                uint32_t * buffer = malloc(buffer_size);
                for (size_t idx = 0; idx < buffer_msg_count; idx++)
                    buffer[idx] = (uint32_t) msg_count;

                size_t bytes_remaining = (msg_count * sizeof(int32_t));
                while (bytes_remaining > 0)
                {
                    size_t bytes_to_send = ((bytes_remaining > buffer_size) ? buffer_size : bytes_remaining);
                    ssize_t bytes_sent = send(sockfd, (const char*) buffer, bytes_to_send, 0);
                    if (bytes_sent < 0)
                    {
                        fprintf(stderr, "send() failed\n");
                        break;
                    }
                    bytes_remaining -= bytes_sent;
                }

                free(buffer);
            }
            else
            {
                fprintf(stderr, "connect() failed\n");
                zv_acceptor_stop(acceptor, &after_acceptor_stop, NULL);
            }

            zv_socket_close(sockfd);
        }
        else
        {
            fprintf(stderr, "socket() failed\n");
            zv_acceptor_stop(acceptor, &after_acceptor_stop, NULL);
            rc = -1;
        }
    }
    else
    {
        printf("start_acceptor_with_buffer_cache() failed\n");
        zv_loop_stop(loop);
        rc = -1;
    }

    pthread_join(server_thread, NULL);

    return rc;
}
