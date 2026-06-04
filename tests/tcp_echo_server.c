#include "zv/acceptor.h"
#include "zv/array.h"
#include "zv/buffer.h"
#include "zv/channel.h"
#include "helpers.h"
#include "tcp_echo_server.h"
#include "rs_echo_server.h"

#include <assert.h>
#include <stdio.h>
#include <errno.h>

struct tcp_echo_server_zv_s
{
    zv_loop_t * loop;
    zv_buffer_cache_t * buffer_cache;
    zv_acceptor_t * acceptor;
    pthread_t thread;
};


struct tcp_echo_server_s
{
    int use_libzv;
    union
    {
        struct tcp_echo_server_zv_s zv;
        rs_echo_server_t * rs;
    };
};


static void tcp_echo_server_input_handler(
    zv_channel_t * channel,
    const void * data,
    size_t size
) {
    zv_channel_send_sync(channel, data, size);
}


static void tcp_echo_server_close_handler(zv_channel_t * channel)
{
    /*
    printf(
        "%s -> %s: connection closed\n",
        zv_channel_get_local_addr_str(channel),
        zv_channel_get_peer_addr_str(channel)
    );
    */
}


static void tcp_echo_server_accept_handler(zv_acceptor_t * acceptor, zv_socket_t sockfd)
{
    struct tcp_echo_server_zv_s * server = zv_acceptor_get_extra_data(acceptor);
    zv_channel_t * channel = zv_channel_create(
        server->loop,
        server->buffer_cache,
        sockfd,
        &tcp_echo_server_input_handler,
        &tcp_echo_server_close_handler,
        server
    );

    if (channel == NULL)
    {
        fprintf(stderr, "zv_channel_create() failed\n");
        zv_socket_close(sockfd);
    }
    else
    {
        /*
        printf(
            "%s -> %s: connection accepted\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );
        */
        zv_channel_start(channel);
        zv_channel_release(channel);
    }
}


static void * tcp_echo_server_zv_loop_thread_func(void * arg)
{
    struct tcp_echo_server_zv_s * server = arg;
    zv_loop_run(server->loop);
    zv_acceptor_destroy(server->acceptor);
    zv_buffer_cache_release(server->buffer_cache);
    zv_loop_destroy(server->loop);
    return NULL;
}


static int tcp_echo_server_zv_init(
    struct tcp_echo_server_s* server,
    struct zv_loop_config_s* loop_config,
    in_port_t* port
) {
    zv_loop_t* loop = zv_loop_create(loop_config);
    if (loop == NULL)
        return -1;

    zv_buffer_cache_t* buffer_cache = zv_buffer_cache_create(1024 * 8, 8, 2);
    if (buffer_cache)
    {
        struct sockaddr_in addr = make_sockaddr_in(INADDR_ANY, 0);
        zv_acceptor_t * acceptor = zv_acceptor_create(
            loop,
            (const struct sockaddr*)&addr,
            sizeof(addr),
            tcp_echo_server_accept_handler,
            &server->zv
        );

        if (acceptor != NULL)
        {
            *port = zv_acceptor_get_port(acceptor);
            server->zv.loop = loop;
            server->zv.buffer_cache = buffer_cache;
            server->zv.acceptor = acceptor;

            int rc = pthread_create(
                &server->zv.thread,
                NULL,
                &tcp_echo_server_zv_loop_thread_func,
                &server->zv
            );

            if (rc == 0)
            {
                zv_acceptor_start(acceptor);
                printf(
                    "TCP echo server (ZV/%s) started on port %hu\n",
                    zv_loop_type_get_name(zv_loop_get_type(loop)),
                    *port
                );

                return 0;
            }

            zv_acceptor_destroy(acceptor);
        }
        else
            fprintf(stderr, "zv_acceptor_create() failed\n");

        zv_buffer_cache_release(buffer_cache);
    }
    else
        fprintf(stderr, "zv_buffer_cache_create() failed\n");

    zv_loop_destroy(loop);
    return -1;
}


tcp_echo_server_t * tcp_echo_server_create(
    struct zv_loop_config_s * loop_config,
    in_port_t * port
) {
    struct tcp_echo_server_s * server = malloc(sizeof(struct tcp_echo_server_s));
    if (server == NULL)
        return NULL;

    int rc;
    if (loop_config == NULL)
    {
        server->use_libzv = 0;
        rs_echo_server_t * rs = rs_echo_server_create(port);
        if (rs == NULL)
        {
            fprintf(stderr, "rs_echo_server_create() failed\n");
            rc = -1;
        }
        else
        {
            server->rs = rs;
            rc = 0;
        }
    }
    else
    {
        server->use_libzv = 1;
        rc = tcp_echo_server_zv_init(server, loop_config, port);
    }

    if (rc == 0)
        return server;
    else
    {
        free(server);
        return NULL;
    }
}


void tcp_echo_server_stop_and_destroy(tcp_echo_server_t * server)
{
    if (server->use_libzv)
    {
        zv_loop_stop(server->zv.loop);
        pthread_join(server->zv.thread, NULL);
    }
    else
        rs_echo_server_stop_and_destroy(server->rs);
    free(server);
}
