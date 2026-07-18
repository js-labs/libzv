#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/log.h"
#include "zv/os_time.h"
#include "zv/semaphore.h"
#include "helpers.h"

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

#define ARRAY_SIZE(__array) (sizeof((__array))/sizeof((__array)[0]))
#define NUM_SEND_MSGS (5 * 1000 * 2048)

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


struct message_header_s
{
    size_t message_size;
    size_t num_messages;
    struct timespec start_time;
};


static void prepare_message(
    void * message,
    size_t message_size,
    size_t num_messages,
    const struct timespec * start_time
) {
    struct message_header_s message_header =
    {
        .message_size = message_size,
        .num_messages = num_messages,
        .start_time = *start_time
    };

    char * ptr = message;
    memcpy(ptr, &message_header, sizeof(message_header));
    ptr += sizeof(message_header);

    size_t count = (message_size - sizeof(message_header));
    for (size_t idx=0; idx<count; idx++)
        ptr[idx] = (char)idx;
}


struct sender_args_s
{
#if defined(ZV_CHANNEL_SEND_REF)
    int send_ref;
#endif
    size_t message_size;
    size_t num_messages;
    long send_time_ms;
};


static void test_send_data(zv_channel_t * channel, struct sender_args_s * sender_args)
{
    size_t message_size = sender_args->message_size;
    size_t num_messages = sender_args->num_messages;
    assert(message_size >= sizeof(struct message_header_s));

    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    char * message = malloc(message_size);
    if (message == NULL)
    {
        fprintf(stderr, "malloc() failed\n");
        abort();
    }
    prepare_message(message, message_size, num_messages, &start_time);

    size_t count = num_messages;
    for (; count > 0; count--)
    {
        int rc = zv_channel_send(channel, message, message_size);
        if (rc != 0)
        {
            fprintf(stderr, "zv_channel_send() failed\n");
            break;
        }
    }

    if (count == 0)
    {
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        sender_args->send_time_ms = zv_elapsed_time_us(&start_time, &end_time) / 1000;
    }

    free(message);
}

#if defined(ZV_CHANNEL_SEND_REF)

static void after_send(void * thunk)
{
    _Atomic size_t * buffer_ref_cnt = thunk;
    size_t rc = atomic_fetch_sub(buffer_ref_cnt, 1) - 1;
    if (rc == 0)
        free(thunk);
}

static void test_send_ref(zv_channel_t * channel, struct sender_args_s * sender_args)
{
    size_t message_size = sender_args->message_size;
    size_t num_messages = sender_args->num_messages;
    assert(message_size >= sizeof(struct message_header_s));

    void * buffer = malloc(sizeof(size_t) + message_size);
    if (buffer == NULL)
        abort();
    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    void * message = ((char*)buffer) + sizeof(size_t);
    prepare_message(message, message_size, num_messages, &start_time);

    _Atomic size_t * buffer_ref_cnt = buffer;
    atomic_store(buffer_ref_cnt, (num_messages + 1));
    size_t count = num_messages;
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
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        long elapsed_time_ms = zv_elapsed_time_us(&start_time, &end_time) / 1000;
        sender_args->send_time_ms = elapsed_time_ms;
    }

    if (atomic_fetch_sub(buffer_ref_cnt, 1) == 1)
        free(buffer);
}

#endif

static void after_connect(zv_connector_t * connector, zv_socket_t sockfd)
{
    zv_loop_t * loop = zv_connector_get_loop(connector);
    struct sender_args_s * sender_args = zv_connector_get_extra_data(connector);
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

    if (channel != NULL)
    {
        printf(
            "Connection %s -> %s: established\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );

        zv_channel_start(channel);

#if defined(ZV_CHANNEL_SEND_REF)
        if (sender_args->send_ref)
            test_send_ref(channel, sender_args);
        else
            test_send_data(channel, sender_args);
#else
        test_send_data(channel, sender_args);
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

    size_t bytes_expected = 0;
    size_t bytes_received = 0;
    struct timespec start_time;

    ssize_t rc = recv(sockfd, buffer, buffer_size, 0);
    if (rc > 0)
    {
        if (rc < sizeof(struct message_header_s))
            abort();
        struct message_header_s message_header;
        memcpy(&message_header, buffer, sizeof(message_header));
        bytes_expected = (message_header.message_size * message_header.num_messages);
        bytes_received = rc;
        start_time = message_header.start_time;

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

    if ((bytes_expected > 0) && (bytes_received == bytes_expected))
    {
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        long elapsed_time_ms = zv_elapsed_time_us(&start_time, &end_time) / 1000;
        *((long*)thunk) = elapsed_time_ms;
    }
    else
    {
        fprintf(stderr, "received %zu bytes instead of %zu...\n", bytes_received, bytes_expected);
        *((long*)thunk) = -1;
    }

    zv_socket_close(sockfd);
}


static void after_connect_fail(zv_connector_t* connector, int sockerr)
{
    // should not happen
    abort();
}


struct result_s
{
    long write_time_ms;
    long e2e_time_ms;
};


static int run_benchmark(
    size_t message_size,
    size_t num_messages,
    struct result_s * result
#if defined(ZV_CHANNEL_SEND_REF)
    , int send_ref
#endif
) {
    result->write_time_ms = -1;
    result->e2e_time_ms = -1;

    in_port_t accept_port;
    pthread_t accept_thread;
    int rc = accept_async(&accept_port, &accept_thread, &after_accept, &result->e2e_time_ms);
    if (rc != 0)
    {
        fprintf(stderr, "async_accept() failed\n");
        return rc;
    }

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

        struct sender_args_s sender_args =
        {
    #if defined(ZV_CHANNEL_SEND_REF)
            .send_ref = send_ref,
    #endif
            .message_size = message_size,
            .num_messages = num_messages,
            .send_time_ms = -1
        };
        zv_connector_set_extra_data(connector, &sender_args);

        if (connector)
        {
            zv_connector_start(connector);
            zv_loop_run(loop);
            result->write_time_ms = sender_args.send_time_ms;
        }
        else
            fprintf(stderr, "zv_connector_create() failed\n");
    }
    else
        fprintf(stderr, "zv_loop_create() failed\n");

    zv_loop_destroy(loop);
    pthread_join(accept_thread, NULL);

    if ((result->write_time_ms == -1) || (result->e2e_time_ms == -1))
        return -1;
    return 0;
}


static void print_results(const char * info, int count, const struct result_s * results)
{
    printf("Send message %s, total data: %u MB\n", info, 128*NUM_SEND_MSGS/(1024*1024));
    printf("            Message    Write      Write          E2E        E2E\n");
    printf("     Msgs   size (B)   time (s)   thrpt (MB/s)   Time (s)   Thrpt (MB/s)\n");
    printf("---------  ---------  ---------  -------------  ---------  -------------\n");
    for (size_t idx=0; idx<count; idx++)
    {
        size_t message_size = 128 * (1 << idx);
        size_t num_messages = (NUM_SEND_MSGS / (1 << idx));
        long write_time_ms = results[idx].write_time_ms;
        long e2e_time_ms = results[idx].e2e_time_ms;
        double total_bytes = (double)message_size * num_messages;
        double write_thrpt = (write_time_ms > 0) ? (total_bytes / (write_time_ms / 1000.0)) / (1024*1024) : 0.0;
        double e2e_thrpt   = (e2e_time_ms  > 0) ? (total_bytes / (e2e_time_ms  / 1000.0)) / (1024*1024) : 0.0;

        printf(
            "%9zu  %9zu  %9.3f %14.1f  %9.3f  %13.1f\n",
            num_messages,
            message_size,
            write_time_ms / 1000.0,
            write_thrpt,
            e2e_time_ms / 1000.0,
            e2e_thrpt
        );
    }
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

    /*
    FILE * file = fopen("channel_send_throughput.log", "w");
    zv_log_backend_t * log_backend = zv_log_add_file_backend(file);
    zv_channel_set_log_level(zv_log_level_debug);
    zv_loop_set_log_level(zv_log_level_debug);
    */

    struct result_s results[4];

#if defined(ZV_CHANNEL_SEND_REF)
    for (int send_ref=0; send_ref<2; send_ref++)
    {
        size_t idx = 0;
        for (; idx<ARRAY_SIZE(results); idx++)
        {
            size_t message_size = 128 * (1 << idx);
            size_t num_messages = (NUM_SEND_MSGS / (1 << idx));
            rc = run_benchmark(message_size, num_messages, &results[idx], send_ref);
            if (rc != 0)
                break;
        }

        if (idx < ARRAY_SIZE(results))
            break;

        const char * info = send_ref ? "by copy" : "by ref";
        print_results(info, ARRAY_SIZE(results), results);
    }
#else
    size_t idx = 0;
    for (; idx<ARRAY_SIZE(results); idx++)
    {
        size_t message_size = 128 * (1 << idx);
        size_t num_messages = (NUM_SEND_MSGS / (1 << idx));
        rc = run_benchmark(message_size, num_messages, &results[idx]);
        if (rc != 0)
            break;
    }

    if (idx == ARRAY_SIZE(results))
        print_results("by copy", ARRAY_SIZE(results), results);
#endif

    /*
    if (log_backend)
    {
        file = zv_log_remove_file_backend(log_backend);
        if (file)
            fclose(file);
    }
    */

#if defined(_WIN32)
    WSACleanup();
#endif

    return rc;
}
