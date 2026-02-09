#include "zv/acceptor.h"
#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/loop.h"
#include <stdint.h>
#include <stdio.h>
#include "helpers.h"

// The client connects to the server and immediately closes the socket.

static void channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    // should not be called
    abort();
}

static void channel_close_handler(zv_channel_t * channel)
{
    printf(
        "connection %s -> %s closed\n",
        zv_channel_get_local_addr_str(channel),
        zv_channel_get_peer_addr_str(channel)
    );

    zv_loop_t * loop = zv_channel_get_loop(channel);
    zv_loop_stop(loop);
}

static void after_acceptor_stop(zv_acceptor_t * acceptor, void * arg)
{
    zv_buffer_cache_t * buffer_cache = zv_acceptor_get_extra_data(acceptor);
    zv_buffer_cache_release(buffer_cache);

    zv_acceptor_destroy(acceptor);

    zv_channel_t * channel = (zv_channel_t*) arg;
    zv_channel_start(channel);
    zv_channel_release(channel);
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
        zv_acceptor_stop(acceptor, &after_acceptor_stop, channel);
    }
    else
    {
        printf("zv_channel_create() failed\n");
        abort();
    }
}

int channel_close1()
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
            if (rc != 0)
            {
                printf("connect() failed\n");
                zv_acceptor_stop(acceptor, &after_acceptor_stop, NULL);
            }
            zv_socket_close(sockfd);
        }
        else
        {
            printf("socket() failed\n");
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
