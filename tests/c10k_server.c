#include "zv/acceptor.h"
#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/loop.h"
#include "zv/os_socket.h"
#include "zv/os_thread.h"
#include "zv/os_time.h"
#include "zv/stream_defragger.h"
#include "zv/timer_pool.h"
#include "helpers.h"
#include "c10k.h"

struct server_s
{
    zv_loop_t * loop;
    zv_buffer_cache_t * buffer_cache;
    zv_acceptor_t * acceptor;
    size_t clients;
};

struct channel_info_s
{
    struct server_s * server;
    zv_channel_t * channel;
    struct zv_stream_defragger_s stream_defragger;
    size_t bytes_received;
};

static size_t message_size_func(const void * header)
{
    return decode_uint16(header);
}

static void server_channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    struct channel_info_s * channel_info = zv_channel_get_extra_data(channel);
    struct zv_stream_defragger_state_s defragger_state = { data, size };
    const size_t header_size = sizeof(uint16_t);

    for (;;)
    {
        size_t message_size;
        const void * message = zv_stream_defragger_get_next(
            &channel_info->stream_defragger,
            header_size,
            &message_size_func,
            &defragger_state,
            &message_size
        );

        if (!message)
            break;

        const void * ptr = (((const char*)message) + header_size);
        uint16_t message_id = decode_uint16(ptr);
        if (message_id == MSG_PING)
        {
            uint8_t pong[sizeof(uint16_t) * 2];
            encode_uint16(&pong[0], sizeof(pong));
            encode_uint16(&pong[sizeof(uint16_t)], MSG_PONG);
            zv_channel_send(channel, pong, sizeof(pong));
        }
        else if (message_id == MSG_PONG)
        {
            // do nothing
        }
        else
        {
            printf(
                "unexpected message %u received from %s\n",
                message_id,
                zv_channel_get_peer_addr_str(channel)
            );
            abort();
        }
    }

    channel_info->bytes_received += size;
}

static void server_channel_close_handler(zv_channel_t * channel)
{
    struct channel_info_s * channel_info = zv_channel_get_extra_data(channel);
    printf(
        "connection accepted: %s -> %s\n",
        zv_channel_get_local_addr_str(channel),
        zv_channel_get_peer_addr_str(channel)
    );

    zv_loop_t * loop = zv_channel_get_loop(channel);
    zv_timer_pool_t * timer_pool = zv_loop_get_timer_pool(loop);
    zv_timer_pool_cancel_sync(timer_pool, channel_info);

    zv_stream_defragger_deinit(&channel_info->stream_defragger);
    free(channel_info);
}

static struct timespec * ping_timer_func(void * arg, struct timespec * repeat_time)
{
    struct channel_info_s * channel_info = arg;
    char ping[sizeof(uint16_t) * 2];
    encode_uint16(&ping[0], sizeof(uint16_t));
    encode_uint16(&ping[sizeof(uint16_t)], MSG_PING);
    zv_channel_send(channel_info->channel, ping, sizeof(ping));
    timespec_now_adding_ms(repeat_time, PING_INTERVAL_SEC*1000);
    return repeat_time;
}

static void accept_handler(zv_acceptor_t * acceptor, zv_socket_t sockfd)
{
    struct server_s * server = zv_acceptor_get_extra_data(acceptor);

    struct channel_info_s * channel_info = malloc(sizeof(struct channel_info_s));
    if (channel_info)
    {
        zv_channel_t * channel = zv_channel_create(
            server->loop,
            server->buffer_cache,
            sockfd,
            &server_channel_input_handler,
            &server_channel_close_handler,
            channel_info
        );

        channel_info->server = server;
        channel_info->channel = channel;
        zv_stream_defragger_init(&channel_info->stream_defragger);
        channel_info->bytes_received = 0;

        if (channel)
        {
            printf(
                "connection accepted: %s -> %s\n",
                zv_channel_get_local_addr_str(channel),
                zv_channel_get_peer_addr_str(channel)
            );

            // we should start timer before zv_channe_start() call
            // because timer will be cancelled in the channel close handler,
            // which can be called earlier than timer will be scheduled.
            //     zv_channel_start(...);
            //        <--- session can be closed here
            //     zv_thread_pool_dispatch_after(...);

            zv_timer_pool_t * timer_pool = zv_loop_get_timer_pool(server->loop);
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ts.tv_sec += PING_INTERVAL_SEC;
            zv_timer_pool_at(timer_pool, &ts, channel_info, ping_timer_func);

            zv_channel_start(channel);
            return;
        }
        else
        {
            free(channel_info);
            printf("zv_channel_create() failed\n");
        }
    }
    else
        printf("malloc(sizeof(struct channel_info_s)) failed\n");
    zv_socket_close(sockfd);
}

static void * server_thread_func(void * arg)
{
    struct server_s * server = arg;
    in_port_t server_port = zv_acceptor_get_port(server->acceptor);
    printf("server started on port %d\n", server_port);
    zv_acceptor_start(server->acceptor);
    zv_loop_run(server->loop);
    zv_loop_destroy(server->loop);
    zv_buffer_cache_release(server->buffer_cache);
    free(server);
    return NULL;
}

int start_server(pthread_t * thread, zv_loop_t ** rloop, in_port_t * port)
{
    zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(1024 * 8, 16, 2);
    if (buffer_cache == NULL)
    {
        printf("zv_buffer_cache_create() failed\n");
        return -1;
    }

    zv_loop_t * loop = zv_loop_create(NULL);
    if (loop)
    {
        struct server_s * server = malloc(sizeof(struct server_s));
        if (server)
        {
            struct sockaddr_in addr = make_sockaddr_in(INADDR_ANY, *port);
            zv_acceptor_t * acceptor = zv_acceptor_create(
                loop,
                (struct sockaddr*) &addr,
                sizeof(addr),
                &accept_handler,
                server
            );

            if (acceptor)
            {
                server->loop = loop;
                server->buffer_cache = buffer_cache;
                server->acceptor = acceptor;
                server->clients = 0;

                int rc = pthread_create(thread, NULL, &server_thread_func, server);
                if (rc == 0)
                {
                    *rloop = loop;
                    if (*port == 0)
                        *port = zv_acceptor_get_port(acceptor);
                    return 0;
                }
                else
                    printf("pthread_create() failed\n");
                zv_acceptor_destroy(acceptor);
            }
            else
                printf("zv_acceptor_create() failed\n");
        }
        else
            printf("malloc(sizeof(struct server_s)) failed\n");
        zv_loop_destroy(loop);
    }
    else
        printf("zv_loop_create() failed\n");
    zv_buffer_cache_release(buffer_cache);

    return -1;
}
