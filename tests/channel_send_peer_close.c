#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/os_time.h"
#include "helpers.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <ws2ipdef.h>
#endif

static void server_channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    // should never be called
    abort();
}

static void after_accept(zv_socket_t sockfd, void * thunk)
{
    struct sockaddr_in peer_addr;
    socklen_t peer_addrlen = sizeof(peer_addr);
    struct sockaddr_in local_addr;
    socklen_t local_addrlen = sizeof(local_addr);
    if (getsockname(sockfd, (struct sockaddr*) &local_addr, &local_addrlen) == 0 &&
        getpeername(sockfd, (struct sockaddr*) &peer_addr, &peer_addrlen) == 0)
    {
        char peer_addr_str[INET_ADDRSTRLEN + 7];
        char local_addr_str[INET_ADDRSTRLEN + 7];
        zv_inet_ntop((struct sockaddr*) &peer_addr, peer_addrlen, peer_addr_str, sizeof(peer_addr_str));
        zv_inet_ntop((struct sockaddr*) &local_addr, local_addrlen, local_addr_str, sizeof(local_addr_str));
        printf("connection accepted: %s -> %s\n", local_addr_str, peer_addr_str);

        uint32_t msg;
        recv(sockfd, (char*) &msg, sizeof(msg), 0);
        usleep(500);
    }
    zv_socket_close(sockfd);
}

static void channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    // no any data expected
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

static void after_connect(zv_connector_t * connector, zv_socket_t sockfd)
{
    zv_loop_t * loop = zv_connector_get_loop(connector);

    zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(1024 * 8, 16, 4);
    if (buffer_cache == NULL)
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

    if (channel != NULL)
    {
        printf(
            "connection established: %s -> %s\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );

        zv_channel_start(channel);

        uint16_t data[512];
        for (uint16_t idx=0; idx<512; idx++)
            data[idx] = idx;

        size_t bytes_sent = 0;
        for (;;)
        {
            int rc = zv_channel_send(channel, data, sizeof(data));
            if (rc)
            {
                printf("zv_channel_send() failed, %zu bytes sent (expected)\n", bytes_sent);
                break;
            }
            bytes_sent += sizeof(data);
        }

        zv_channel_release(channel);
    }
}

int channel_send_peer_close()
{
    in_port_t accept_port;
    pthread_t accept_thread;
    int rc = accept_async(&accept_port, &accept_thread, &after_accept, NULL);
    if (rc == 0)
    {
        printf("server started at port %hu\n", accept_port);

        zv_loop_t * loop = create_loop_and_connector(accept_port, &after_connect);
        zv_loop_run(loop);
        zv_loop_destroy(loop);

        pthread_join(accept_thread, NULL);
    }
    else
        fprintf(stderr, "accept_async() failed\n");
    return 0;
}
