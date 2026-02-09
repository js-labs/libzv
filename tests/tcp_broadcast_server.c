#include "zv/acceptor.h"
#include "zv/array.h"
#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/os_compat.h"
#include "helpers.h"
#include "tcp_broadcast_server.h"

struct tcp_broadcast_server_s
{
    pthread_t thread;
    zv_loop_t * loop;
    zv_buffer_cache_t * buffer_cache;
    zv_acceptor_t * acceptor;

    pthread_mutex_t mutex;
    zv_channel_t ** channels;
    int stop;
};


static size_t server_add_client_channel(
    struct tcp_broadcast_server_s * server,
    zv_channel_t * channel
) {
    zv_channel_retain(channel);
    pthread_mutex_lock(&server->mutex);
    *zv_array_append(server->channels) = channel;
    size_t count = zv_array_get_size(server->channels);
    pthread_mutex_unlock(&server->mutex);
    return count;
}


static void server_remove_client_channel(
    struct tcp_broadcast_server_s * server,
    zv_channel_t * channel
) {
    pthread_mutex_lock(&server->mutex);
    size_t count = zv_array_get_size(server->channels);
    size_t idx = 0;
    for (; idx<count; idx++)
    {
        if (server->channels[idx] == channel)
        {
            if ((idx + 1) < count)
                 server->channels[idx] = server->channels[count - 1];
            zv_array_pop(server->channels);
            count;
            break;
        }
    }
    int stop = server->stop;
    pthread_mutex_unlock(&server->mutex);

    if (idx < count)
    {
        zv_channel_release(channel);
        if ((count == 1) && stop)
            zv_loop_stop(server->loop);
    }
    else
        abort(); // something unexpected
}


static void server_broadcast(
    struct tcp_broadcast_server_s * server,
    zv_channel_t * from_channel,
    const void * data,
    size_t size
) {
    zv_channel_t * channels[64];
    zv_channel_t ** pch = &channels[0];
    pthread_mutex_lock(&server->mutex);
    size_t count = zv_array_get_size(server->channels);
    for (size_t idx=0; idx<count; idx++)
    {
        zv_channel_t * channel = server->channels[idx];
        if (channel != from_channel)
        {
            zv_channel_retain(channel);
            *pch = channel;
            pch++;
        }
    }
    pthread_mutex_unlock(&server->mutex);

    for (size_t idx=0; idx<count-1; idx++)
    {
        zv_channel_t * channel = channels[idx];
        zv_channel_send(channel, data, size);
        zv_channel_release(channel);
    }
}


static void channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    struct tcp_broadcast_server_s * server = zv_channel_get_extra_data(channel);
    server_broadcast(server, channel, data, size);
}


static void channel_close_handler(zv_channel_t * channel)
{
    printf(
        "%s -> %s: connection closed\n",
        zv_channel_get_local_addr_str(channel),
        zv_channel_get_peer_addr_str(channel)
    );
    struct tcp_broadcast_server_s * server = zv_channel_get_extra_data(channel);
    server_remove_client_channel(server, channel);
}


static void server_accept_handler(zv_acceptor_t * acceptor, zv_socket_t sockfd)
{
    struct tcp_broadcast_server_s * server = zv_acceptor_get_extra_data(acceptor);
    zv_channel_t * channel = zv_channel_create(
        server->loop,
        server->buffer_cache,
        sockfd,
        &channel_input_handler,
        &channel_close_handler,
        server
    );

    if (channel != NULL)
    {
        printf(
            "%s -> %s: connection accepted\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );

        size_t count = server_add_client_channel(server, channel);
        zv_channel_start(channel);

        struct msg_connected_clients_s connected_clients;
        connected_clients.size = sizeof(connected_clients);
        connected_clients.id = MSG_CONNECTED_CLIENTS;
        connected_clients.count = (uint16_t) count;
        zv_channel_send(channel, &connected_clients, sizeof(connected_clients));

        zv_channel_release(channel);
    }
    else
    {
        fprintf(stderr, "zv_channel_create() failed\n");
        zv_socket_close(sockfd);
    }
}


static void server_after_acceptor_stop(zv_acceptor_t * acceptor, void * arg)
{
    // now no new connections will be established
    struct tcp_broadcast_server_s * server = zv_acceptor_get_extra_data(acceptor);
    pthread_mutex_lock(&server->mutex);
    size_t count = zv_array_get_size(server->channels);
    if (count > 0)
    {
        for (size_t idx=0; idx<count; idx++)
            zv_channel_stop(server->channels[idx]);
        pthread_mutex_unlock(&server->mutex);
    }
    else
    {
        pthread_mutex_unlock(&server->mutex);
        zv_loop_stop(server->loop);
    }
}


static void * server_loop_thread_func(void * arg)
{
    struct tcp_broadcast_server_s * server = arg;
    zv_loop_run(server->loop);
    return NULL;
}


tcp_broadcast_server_t * tcp_broadcast_server_create(
    struct zv_loop_config_s * loop_config,
    in_port_t * port
) {
    struct tcp_broadcast_server_s * server = malloc(sizeof(struct tcp_broadcast_server_s));
    if (server == NULL)
    {
        fprintf(stderr, "malloc() failed\n");
        return NULL;
    }

    zv_loop_t * loop = zv_loop_create(loop_config);
    if (loop != NULL)
    {
        zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(1024*32, 8, 8);
        if (buffer_cache != NULL)
        {
            struct sockaddr_in addr = make_sockaddr_in(INADDR_ANY, *port);
            zv_acceptor_t * acceptor = zv_acceptor_create(
                loop,
                (const struct sockaddr*) &addr,
                sizeof(addr),
                &server_accept_handler,
                server
            );

            if (acceptor != NULL)
            {
                if (*port == 0)
                    *port = zv_acceptor_get_port(acceptor);

                server->loop = loop;
                server->buffer_cache = buffer_cache;
                server->acceptor = acceptor;

                int rc = pthread_mutex_init(&server->mutex, NULL);
                if (rc == 0)
                {
                    server->channels = NULL;
                    server->stop = 0;

                    rc = pthread_create(
                        &server->thread,
                        NULL,
                        &server_loop_thread_func,
                        server
                    );

                    if (rc == 0)
                    {
                        zv_acceptor_start(acceptor);
                        printf("broadcast server started on port %hu\n", *port);
                        return server;
                    }
                    else
                        fprintf(stderr, "pthread_create() failed\n");

                    pthread_mutex_destroy(&server->mutex);
                }
                else
                    fprintf(stderr, "pthread_mutex_init() failed\n");

                zv_acceptor_destroy(acceptor);
            }
            else
                fprintf(stderr, "zv_acceptor_create() failed\n");

            zv_buffer_cache_release(buffer_cache);
        }
        else
            fprintf(stderr, "zv_buffer_cache_create() failed\n");

        zv_loop_destroy(loop);
    }
    else
        fprintf(stderr, "zv_loop_create() failed\n");

    free(server);
    return NULL;
}


void tcp_broadcast_server_stop_and_destroy(tcp_broadcast_server_t * server)
{
    pthread_mutex_lock(&server->mutex);
    server->stop = 1;
    pthread_mutex_unlock(&server->mutex);

    zv_acceptor_stop(server->acceptor, &server_after_acceptor_stop, NULL);
    pthread_join(server->thread, NULL);

    zv_acceptor_destroy(server->acceptor);
    zv_buffer_cache_release(server->buffer_cache);
    zv_loop_destroy(server->loop);
    zv_array_destroy(server->channels);
    pthread_mutex_destroy(&server->mutex);
    free(server);
}
