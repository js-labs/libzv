#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/os_time.h"
#include "helpers.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

struct test_s
{
    size_t sessions;
    size_t messages;
    size_t message_size;
    zv_buffer_cache_t * buffer_cache;
    _Atomic size_t sessions_done;
};

static struct test_s * test_create(size_t sessions, size_t messages, size_t message_size)
{
    struct test_s * test = malloc(sizeof(struct test_s));
    zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(1024*8, 16, 2);
    if (buffer_cache)
    {
        test->sessions = sessions;
        test->messages = messages;
        test->message_size = message_size;
        test->buffer_cache = buffer_cache;
        test->sessions_done = 0;
        return test;
    }
    free(test);
    return NULL;
}


static void test_destroy(struct test_s * test)
{
    zv_buffer_cache_release(test->buffer_cache);
    free(test);
}


static void after_acceptor_stop(zv_acceptor_t * acceptor, void * arg)
{
    zv_loop_t * loop = zv_acceptor_get_loop(acceptor);
    zv_loop_stop(loop);
    zv_acceptor_destroy(acceptor);
}


static void test_session_done(struct test_s * test, zv_acceptor_t * acceptor)
{
    size_t sessions_done = atomic_fetch_add(&test->sessions_done, 1) + 1;
    if (sessions_done == test->sessions)
    {
        test_destroy(test);
        zv_acceptor_stop(acceptor, &after_acceptor_stop, NULL);
    }
}


struct channel_extra_data_s
{
    struct test_s * test;
    zv_acceptor_t * acceptor;
    size_t bytes_received;
    struct timespec start_time;
};


static void channel_input_handler(zv_channel_t * channel, const void * data, size_t size)
{
    struct channel_extra_data_s * extra_data = zv_channel_get_extra_data(channel);
    struct test_s * test = extra_data->test;

    if (extra_data->bytes_received == 0)
        clock_gettime(CLOCK_MONOTONIC, &extra_data->start_time);

    extra_data->bytes_received += size;
    size_t bytes_expected = (test->messages * test->message_size);

    if (extra_data->bytes_received >= bytes_expected)
    {
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);

        long elapsed_time_ms = zv_elapsed_time_us(&extra_data->start_time, &end_time) / 1000;
        printf(
            "received %zu bytes in %ld.%.3ld sec\n",
            bytes_expected,
            elapsed_time_ms / 1000,
            elapsed_time_ms % 1000
        );

        char r = 0;
        zv_channel_send(channel, &r, sizeof(r));
    }
}


static void channel_close_handler(zv_channel_t * channel)
{
    printf(
        "session %s -> %s: closed\n",
        zv_channel_get_local_addr_str(channel),
        zv_channel_get_peer_addr_str(channel)
    );
    struct channel_extra_data_s * extra_data = zv_channel_get_extra_data(channel);
    test_session_done(extra_data->test, extra_data->acceptor);
    free(extra_data);
}


static void accept_handler(zv_acceptor_t * acceptor, zv_socket_t sockfd)
{
    struct test_s * test = zv_acceptor_get_extra_data(acceptor);
    struct channel_extra_data_s * channel_extra_data = malloc(sizeof(struct channel_extra_data_s));
    channel_extra_data->test = test;
    channel_extra_data->acceptor = acceptor;
    channel_extra_data->bytes_received = 0;

    zv_channel_t * channel = zv_channel_create(
        zv_acceptor_get_loop(acceptor),
        test->buffer_cache,
        sockfd,
        channel_input_handler,
        channel_close_handler,
        channel_extra_data
    );

    if (channel)
    {
        printf(
            "session %s -> %s: accepted\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );

        zv_channel_start(channel);
        zv_channel_release(channel);
    }
    else
        test_session_done(test, acceptor);
}


struct sender_thread_arg_s
{
    size_t id;
    in_port_t server_port;
    size_t messages;
    size_t message_size;
};


static void * sender_thread_func(void * _arg)
{
    struct sender_thread_arg_s * arg = (struct sender_thread_arg_s*) _arg;

    // will send data by blocks of ~ 32kb
    size_t message_size = arg->message_size;
    size_t messages = arg->messages;
    size_t data_to_send = (message_size * messages);
    size_t block_size = (1024 * 32);
    if (block_size > data_to_send)
        block_size = data_to_send;
    else
        block_size = ((block_size / message_size) * message_size);
    char * block = malloc(block_size);
    memset(block, 42, block_size);
    zv_socket_t sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd != INVALID_SOCKET)
    {
        struct sockaddr_in addr = make_sockaddr_in(INADDR_LOOPBACK, arg->server_port);
        int rc = connect(sockfd, (const struct sockaddr*) &addr, sizeof(addr));
        if (rc == 0)
        {
            size_t bytes_remaining = data_to_send;
            while (bytes_remaining > 0)
            {
                size_t size = (bytes_remaining < block_size) ? bytes_remaining : block_size;
#if defined(_WIN32)
                int bytes_sent = send(sockfd, block, (int)size, 0);
#else
                ssize_t bytes_sent = send(sockfd, block, size, 0);
#endif
                if (bytes_sent < 0)
                    break;
                bytes_remaining -= bytes_sent;
            }

            if (bytes_remaining == 0)
            {
                char c;
                recv(sockfd, &c, sizeof(c), 0);
            }
            else
                printf("send() failed\n");
        }
        else
            printf("connect() failed\n");
        zv_socket_close(sockfd);
    }
    else
        printf("socket() failed\n");

    free(block);
    free(arg);
    return NULL;
}


static int parse_args(int argc, const char * argv[], size_t * sessions)
{
    int idx = 1;
    for (;;)
    {
        if (idx == argc)
            return 0;
        const char * arg = argv[idx];
        if (strcmp(arg, "-s") == 0)
        {
            if (++idx == argc)
            {
                fprintf(stderr, "Error: Missing value for <number_of_sessions> after '-s'.\n");
                break;
            }
            arg = argv[idx];
            int rc = sscanf(arg, "%zu", sessions);
            if (rc != 1)
            {
                fprintf(stderr, "Error: Invalid value '%s' for <number_of_sessions>.\n", arg);
                break;
            }
        }
        else
        {
            fprintf(stderr, "Error: Unsupported command line option '%s'.\n", arg);
            break;
        }
        idx++;
    }
    fprintf(stderr, "Usage: %s [-s <number_of_sessions>]\n", argv[0]);
    return -1;
}


int main(int argc, const char * argv[])
{
    size_t sessions = 1;
    size_t messages = (1000 * 1000);
    size_t message_size = 300;

    int rc = parse_args(argc, argv, &sessions);
    if (rc != 0)
        return rc;

    printf("running test with %zu sessions\n", sessions);

    /*
    zv_log_add_file_backend(stdout);
    zv_acceptor_set_log_level(zv_log_level_debug);
    zv_channel_set_log_level(zv_log_level_debug);
    zv_loop_set_log_level(zv_log_level_debug);
    */

    pthread_t loop_thread;
    zv_loop_t * loop = start_loop(&loop_thread, NULL);
    if (!loop)
    {
        printf("start_loop1() failed\n");
        return -1;
    }

    struct test_s * test = test_create(sessions, messages, message_size);
    if (test != NULL)
    {
        struct sockaddr_in addr = make_sockaddr_in(INADDR_ANY, 0);
        zv_acceptor_t * acceptor = zv_acceptor_create(
            loop,
            (const struct sockaddr*) &addr,
            sizeof(addr),
            accept_handler,
            test
        );

        if (acceptor)
        {
            in_port_t port = zv_acceptor_get_port(acceptor);
            printf("server started at port %hu\n", port);
            zv_acceptor_start(acceptor);

            for (size_t idx=0; idx<sessions; idx++)
            {
                struct sender_thread_arg_s * sender_thread_arg;
                sender_thread_arg = malloc(sizeof(struct sender_thread_arg_s));
                sender_thread_arg->id = idx;
                sender_thread_arg->server_port = port;
                sender_thread_arg->messages = messages;
                sender_thread_arg->message_size = message_size;

                pthread_t sender_thread;
                rc = pthread_create(&sender_thread, NULL, &sender_thread_func, sender_thread_arg);
                if (rc != 0)
                {
                    free(sender_thread_arg);
                    test_session_done(test, acceptor);
                }
            }
        }
        else
        {
            printf("zv_acceptor_create() failed\n");
            test_destroy(test);
            zv_loop_stop(loop);
            rc = -1;
        }
    }
    else
    {
        printf("zv_buffer_cache_create() failed\n");
        zv_loop_stop(loop);
        rc = -1;
    }

    pthread_join(loop_thread, NULL);
    return rc;
}
