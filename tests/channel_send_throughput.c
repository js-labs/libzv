#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/log.h"
#include "zv/os_time.h"
#include "helpers.h"

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

static const char SEND_DATA[] = "Hello, world.";
#define NUM_SEND_MSGS (10 * 1000 * 1000)


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


static void test_send_data(zv_channel_t * channel)
{
    struct timespec ts_t1;
    clock_gettime(CLOCK_MONOTONIC, &ts_t1);

    char message[sizeof(struct timespec) + sizeof(SEND_DATA)];
    memcpy(message, &ts_t1, sizeof(struct timespec));
    memcpy(message + sizeof(struct timespec), SEND_DATA, sizeof(SEND_DATA));

    int count = NUM_SEND_MSGS;
    for (; count > 0; count--)
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
            "> sent %d messages by copy (%zu bytes) in %ld.%.3ld sec\n",
            NUM_SEND_MSGS,
            sizeof(message) * NUM_SEND_MSGS,
            elapsed_time_ms / 1000,
            elapsed_time_ms % 1000
        );
    }
}

#if defined(ZV_CHANNEL_SEND_REF)

static void after_send(void * thunk)
{
    _Atomic size_t * buffer_ref_cnt = thunk;
    size_t rc = atomic_fetch_sub(buffer_ref_cnt, 1) - 1;
    if (rc == 0)
        free(thunk);
}

static void test_send_ref(zv_channel_t * channel)
{
    size_t message_size = sizeof(struct timespec) + sizeof(SEND_DATA);
    void * buffer = malloc(sizeof(size_t) + message_size);
    if (buffer == NULL)
        abort();
    struct timespec ts_t1;
    clock_gettime(CLOCK_MONOTONIC, &ts_t1);

    void * message = ((char*)buffer) + sizeof(size_t);
    memcpy(message, &ts_t1, sizeof(ts_t1));
    memcpy(((char*)message) + sizeof(ts_t1), SEND_DATA, sizeof(SEND_DATA));

    size_t count = NUM_SEND_MSGS;
    _Atomic size_t * buffer_ref_cnt = buffer;
    *buffer_ref_cnt = count;
    for (; count>0; count--)
    {
        int rc = zv_channel_send_ref(channel, message, message_size, &after_send, buffer);
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
            "> sent %d messages by ref (%zu bytes) in %ld.%.3ld sec\n",
            NUM_SEND_MSGS,
            message_size * NUM_SEND_MSGS,
            elapsed_time_ms / 1000,
            elapsed_time_ms % 1000
        );
    }
}

#endif


static void after_connect(zv_connector_t * connector, zv_socket_t sockfd)
{
    zv_loop_t * loop = zv_connector_get_loop(connector);
#if defined(ZV_CHANNEL_SEND_REF)
    int * send_ref = zv_connector_get_extra_data(connector);
#endif
    zv_connector_destroy(connector);

    zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(16*1024, 16, 4);
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

    if (channel)
    {
        printf(
            "Connection %s -> %s: established\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );

        zv_channel_start(channel);

#if defined(ZV_CHANNEL_SEND_REF)
        if (*send_ref)
            test_send_ref(channel);
        else
            test_send_data(channel);
#else
        send_test_data(channel);
#endif

        zv_channel_release(channel);
    }
}


static void after_accept(zv_socket_t sockfd, void * thunk)
{
    size_t buffer_size = (32 * 1024);
    char * buffer = malloc(buffer_size);
    if (buffer == NULL)
    {
        fprintf(stderr, "malloc() failed\n");
        abort();
    }

    const size_t bytes_expected = ((sizeof(struct timespec) + sizeof(SEND_DATA)) * NUM_SEND_MSGS);
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
}


static void after_connect_fail(zv_connector_t* connector, int sockerr)
{
    // should not happen
    abort();
}


int main(int argc, const char * argv[])
{
    in_port_t accept_port;
    pthread_t accept_thread;
    int rc;

#if defined(_WIN32)
    if (wsa_init() != 0)
        return -1;
#endif

    FILE * file = fopen("channel_send_throughput.log", "w");
    zv_log_backend_t * log_backend = zv_log_add_file_backend(file);
    zv_channel_set_log_level(zv_log_level_debug);
    zv_loop_set_log_level(zv_log_level_debug);

#if defined(ZV_CHANNEL_SEND_REF)
    int send_ref = 0;
#endif

    rc = accept_async(&accept_port, &accept_thread, &after_accept, NULL);
    if (rc == 0)
    {
        printf("server started at port %hu\n", accept_port);

        zv_loop_t * loop = zv_loop_create(NULL);
        if (loop != NULL)
        {
            struct sockaddr_in addr = make_sockaddr_in(INADDR_LOOPBACK, accept_port);
            zv_connector_t * connector = zv_connector_create(
                loop,
                (struct sockaddr*) &addr,
                sizeof(addr),
                after_connect,
                after_connect_fail,
                NULL
            );

        #if defined(ZV_CHANNEL_SEND_REF)
            zv_connector_set_extra_data(connector, &send_ref);
        #endif

            if (connector)
            {
                zv_connector_start(connector);
                zv_loop_run(loop);
            }
            else
                fprintf(stderr, "zv_connector_create() failed\n");
        }
        else
            fprintf(stderr, "zv_loop_create() failed\n");

        zv_loop_destroy(loop);
        pthread_join(accept_thread, NULL);
    }
    else
        fprintf(stderr, "accept_async() failed\n");

    if (log_backend)
    {
        file = zv_log_remove_file_backend(log_backend);
        if (file)
            fclose(file);
    }

#if defined(_WIN32)
    WSACleanup();
#endif

    return rc;
}
