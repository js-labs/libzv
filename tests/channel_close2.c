#include "zv/acceptor.h"
#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/loop.h"
#include "zv/os_time.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "helpers.h"

// Client establishes connection to the server,
// send some data and then close the socket
// imitating the situation when the socket is closed
// while the channel inbound queue is not empty

static void channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    if (size != sizeof(uint16_t))
        abort();
    uint16_t msg = *((const uint16_t*) data);
    if (msg != 0x4242)
        abort();

    // Hope 200 milliseconds will be enough
    // to catch the situation when the socket is closed
    // while the input handler is not finished
    const unsigned int sleep_time_ms = 200;
    usleep(sleep_time_ms * 1000);
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

        zv_channel_start(channel);
        zv_channel_release(channel);
    }
    else
    {
        printf("zv_channel_create() failed\n");
        abort();
    }
}

int channel_close2()
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
                uint16_t msg = 0x4242;
                send(sockfd, (const char*) & msg, sizeof(msg), 0);
            }
            else
            {
                printf("connect() failed\n");
                zv_acceptor_stop(acceptor, &after_acceptor_stop, NULL);
            }

            // There is no guarantee that the other side will receive the data if we close
            // the socket immediately after the send() call, but it often works anyway.
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
        printf("create_acceptor_with_buffer_cache() failed\n");
        zv_loop_stop(loop);
        rc = -1;
    }

    pthread_join(server_thread, NULL);

    return rc;
}
