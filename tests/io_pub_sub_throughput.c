#include "uio/acceptor.h"
#include "uio/atomic.h"
#include "uio/buffer.h"
#include "uio/channel.h"
#include "uio/connector.h"
#include "uio/reactor.h"
#include "uio/stream_defragger.h"
#include "uio/thread.h"
#include "uio/thread_pool.h"
#include "uio/time.h"
#include <assert.h>


struct test_s
{
    uio_reactor_t * reactor;

    size_t subscribers;
    size_t message_size;
    size_t warmup_messages;
    size_t messages;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    size_t clients_connected;
    uio_channel_t ** clients;
    size_t clients_done;

    pthread_t sender_thread;

    struct timeval start_time;
};


struct channel_extra_data_s
{
    struct test_s * test;
    uio_stream_defragger_t * stream_defragger;
};


static struct test_s * test_create(uio_reactor_t * reactor,
    size_t subscribers, size_t message_size, size_t warmup_messages, size_t messages)
{
    struct test_s * test = malloc(sizeof(struct test_s));
    test->reactor = reactor;
    test->subscribers = subscribers;
    test->message_size = message_size;
    test->warmup_messages = warmup_messages;
    test->messages = messages;
    pthread_mutex_init(&test->mutex, NULL);
    pthread_cond_init(&test->cond, NULL);
    test->clients_connected = 0;
    test->clients = malloc(sizeof(uio_channel_t*)*(subscribers + 1));
    test->clients_done = 0;
    return test;
}


static void test_destroy(struct test_s * test)
{
    pthread_cond_destroy(&test->cond);
    pthread_mutex_destroy(&test->mutex);
    free(test->clients);
    free(test);
}


static size_t message_header_handler(const void * ptr)
{
    return *((uint16_t*)ptr);
}


static void server_message_handler(uio_channel_t * channel, const void * data, size_t size)
{
    // propagate received message to clients
    struct channel_extra_data_s * extra_data = uio_channel_get_extra_data(channel);
    struct test_s * test = extra_data->test;
    for (size_t idx=0; idx<test->subscribers; idx++)
        uio_channel_send(test->clients[idx], data, size);
}


static void server_input_handler(uio_channel_t * channel, const void * data, size_t size)
{
    struct channel_extra_data_s * extra_data = uio_channel_get_extra_data(channel);
    uio_stream_defragger_process(channel, data, size, extra_data->stream_defragger, server_message_handler);
}


static void channel_close_handler(uio_channel_t * channel)
{
    assert(0);
}


static void acceptor_channel_handler(uio_acceptor_t * acceptor, uio_channel_t * channel)
{
    printf("Session %s -> %s: accepted\n",
        uio_channel_get_local_addr_str(channel),
        uio_channel_get_remote_addr_str(channel));

    uio_channel_set_input_handler(channel, &server_input_handler);
    uio_channel_set_close_handler(channel, &channel_close_handler);

    struct test_s * test = uio_acceptor_get_extra_data(acceptor);
    struct channel_extra_data_s * extra_data = malloc(sizeof(struct channel_extra_data_s));
    extra_data->test = test;
    extra_data->stream_defragger = uio_stream_defragger_create(sizeof(uint16_t), &message_header_handler);
    uio_channel_set_extra_data(channel, extra_data);

    pthread_mutex_lock(&test->mutex);
    size_t clients_connected = ++test->clients_connected;
    test->clients[clients_connected-1] = channel;
    pthread_mutex_unlock(&test->mutex);

    if (clients_connected == (test->subscribers + 1))
    {
        // send empty message to the last client to start send
        printf("%zd clients connected, start test\n", clients_connected);
        uint16_t msg = sizeof(uint16_t);
        uio_channel_send(channel, &msg, sizeof(msg));
    }
}


static void acceptor_close_handler(uio_acceptor_t * acceptor)
{
    assert(0);
}


struct sender_thread_arg_s
{
    struct test_s * test;
    uio_channel_t * channel;
};


static void * sender_thread(void * arg)
{
    struct sender_thread_arg_s * st = arg;
    struct test_s * test = st->test;
    uio_channel_t * channel = st->channel;
    free(st);

    size_t message_size = test->message_size;
    void * message = malloc(message_size);
    *((uint16_t*)message) = (uint16_t)message_size;
    if (message_size > sizeof(uint16_t))
    {
        unsigned char * ptr = ((unsigned char*)message) + sizeof(uint16_t);
        const size_t count = (message_size - sizeof(uint16_t));
        for (size_t idx=0; idx<count; idx++, ptr++)
            *ptr = (unsigned char)idx;
    }

    for (size_t c=test->warmup_messages; c>0; c--)
        uio_channel_send(channel, message, message_size);

    usleep(100000);
    gettimeofday(&test->start_time, NULL);

    for (size_t c=test->messages-1; c>0; c--)
        uio_channel_send(channel, message, message_size);

    *((char*)message + sizeof(uint16_t)) = 1; // mark the last message
    uio_channel_send(channel, message, message_size);

    free(message);
    return NULL;
}


static void client_message_handler(uio_channel_t * channel, const void * data, size_t size)
{
    const char * ptr = ((const char*)data) + sizeof(uint16_t);
    if (*ptr)
    {
        struct channel_extra_data_s * extra_data = uio_channel_get_extra_data(channel);
        struct test_s * test = extra_data->test;
        size_t clients_done = uio_atomic_add_fetch(&test->clients_done, 1);
        if (clients_done == test->subscribers)
        {
            struct timeval now;
            gettimeofday(&now, NULL);
            double dd = ((now.tv_sec - test->start_time.tv_sec) * 1000000
                       + (now.tv_usec - test->start_time.tv_usec));
            dd /= 1000000;
            printf("publisher -> %zd subscribers, %zd messages, message size = %zd: %.3fs\n",
                test->subscribers, test->messages, test->message_size, dd);
            uio_reactor_stop(test->reactor);
        }
    }
}


static void client_input_handler2(uio_channel_t * channel, const void * data, size_t size)
{
    struct channel_extra_data_s * extra_data = uio_channel_get_extra_data(channel);
    uio_stream_defragger_process(channel, data, size, extra_data->stream_defragger, &client_message_handler);
}


static void client_input_handler1(uio_channel_t * channel, const void * data, size_t size)
{
    if (size >= sizeof(uint16_t))
    {
        uint16_t message_size = *((const uint16_t*)data);
        struct test_s * test = uio_channel_get_extra_data(channel);
        if (message_size == sizeof(uint16_t))
        {
            struct sender_thread_arg_s * arg = malloc(sizeof(struct sender_thread_arg_s));
            arg->test = test;
            arg->channel = channel;
            pthread_create(&test->sender_thread, NULL, &sender_thread, arg);
        }
        else
        {
            struct channel_extra_data_s * extra_data = malloc(sizeof(struct channel_extra_data_s));
            extra_data->test = test;
            extra_data->stream_defragger = uio_stream_defragger_create(sizeof(uint16_t), &message_header_handler);
            uio_channel_set_extra_data(channel, extra_data);
            uio_channel_set_input_handler(channel, &client_input_handler2);
            client_input_handler2(channel, data, size);
        }
    }
    else
        assert(0);
}


static void connector_channel_handler(uio_connector_t * connector, uio_channel_t * channel)
{
    printf("Session %s -> %s: connected\n",
        uio_channel_get_local_addr_str(channel),
        uio_channel_get_remote_addr_str(channel));

    uio_channel_set_input_handler(channel, &client_input_handler1);
    uio_channel_set_extra_data(channel, uio_connector_get_extra_data(connector));
}


int io_pub_sub_throughput()
{
    const size_t SUBSCRIBERS = 5;
    const size_t MESSAGE_SIZE = 500;
    const size_t WARMUP_MESSAGES = 50;
    const size_t MESSAGES = 1000000;

    uio_buffer_cache_t * buffer_cache = uio_buffer_cache_create(1024*32-32, 32, 8);
    uio_thread_pool_t * thread_pool = uio_thread_pool_create(4);
    uio_reactor_t * reactor = uio_reactor_create();

    struct test_s * test = test_create(reactor, SUBSCRIBERS, MESSAGE_SIZE, WARMUP_MESSAGES, MESSAGES);

    uio_acceptor_t * acceptor = uio_acceptor_create(
        reactor, thread_pool, buffer_cache, /*port number*/0,
        &acceptor_channel_handler, &acceptor_close_handler);
    uio_acceptor_set_extra_data(acceptor, test);
    uio_acceptor_start(acceptor);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(uio_acceptor_get_port_number(acceptor));

    size_t clients = (SUBSCRIBERS + 1);
    uio_connector_t ** connector = malloc(sizeof(uio_connector_t*) * clients);
    for (size_t idx=0; idx<clients; idx++)
    {
        uio_connector_t * c = uio_connector_create(
            reactor, thread_pool, buffer_cache, (struct sockaddr*)&addr, sizeof(addr), &connector_channel_handler);
        uio_connector_set_extra_data(c, test);
        uio_connector_start(c);
        connector[idx] = c;
    }

    uio_reactor_run(reactor);
    uio_reactor_destroy(reactor);
    uio_thread_pool_destroy(thread_pool, stdout);
    uio_buffer_cache_destroy(buffer_cache);

    free(connector);
    test_destroy(test);

    return 0;
}
