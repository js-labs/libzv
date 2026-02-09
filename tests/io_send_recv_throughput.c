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

    size_t senders;
    size_t messages;
    size_t message_size;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    int senders_started;
    pthread_t * sender_thread;

    size_t senders_ready;
    struct timeval start_time;
};


static void test_sender_ready(struct test_s * test)
{
    pthread_mutex_lock(&test->mutex);
    if (++test->senders_ready == test->senders)
    {
        gettimeofday(&test->start_time, NULL);
        pthread_cond_broadcast(&test->cond);
    }
    else
    {
        do { pthread_cond_wait(&test->cond, &test->mutex); }
        while (test->senders_ready < test->senders);
    }
    pthread_mutex_unlock(&test->mutex);
}


struct channel_extra_data_s
{
    struct test_s * test;
    uio_stream_defragger_t * stream_defragger;
    size_t messages_received;
};


static struct test_s * test_create(uio_reactor_t * reactor,
    size_t senders, size_t messages, size_t message_size)
{
    struct test_s * test = malloc(sizeof(struct test_s));
    test->reactor = reactor;
    test->senders = senders;
    test->messages = messages;
    test->message_size = message_size;
    pthread_mutex_init(&test->mutex, NULL);
    pthread_cond_init(&test->cond, NULL);
    test->senders_started = 0;
    test->sender_thread = NULL;
    test->senders_ready = 0;
    return test;
}


static void test_destroy(struct test_s * test)
{
    pthread_cond_destroy(&test->cond);
    pthread_mutex_destroy(&test->mutex);
    if (test->sender_thread != NULL)
    {
        for (size_t idx=0; idx<test->senders_started; idx++)
            pthread_join(test->sender_thread[idx], NULL);
        free(test->sender_thread);
    }
    free(test);
}


static size_t message_header_handler(const void * ptr)
{
    return *((uint16_t*)ptr);
}


static void server_input_handler(uio_channel_t * channel, const void * data, size_t size)
{
    assert(0); // should never be called
}


static void server_close_handler(uio_channel_t * channel)
{
    printf("%s -> %s: connection closed\n",
        uio_channel_get_local_addr_str(channel),
        uio_channel_get_remote_addr_str(channel));
}


struct sender_thread_arg_s
{
    size_t id;
    struct test_s * test;
    uio_channel_t * channel;
};


static void * sender_thread(void * arg)
{
    struct sender_thread_arg_s * st_arg = arg;
    size_t id = st_arg->id;
    struct test_s * test = st_arg->test;
    uio_channel_t * channel = st_arg->channel;
    free(st_arg);

    size_t message_size = test->message_size;
    void * message = malloc(message_size);
    char * ptr = message;
    *((uint16_t*)ptr) = (uint16_t) message_size;
    ptr += sizeof(uint16_t);
    *((uint32_t*)ptr) = (uint32_t) (test->senders * test->messages);

    if (message_size > (sizeof(uint16_t) + sizeof(uint32_t)))
    {
        ptr += sizeof(uint32_t);
        const size_t cc = (message_size - sizeof(uint16_t) - sizeof(uint32_t));
        for (size_t idx=0; idx<cc; idx++, ptr++)
            *ptr = (unsigned char)idx;
    }

    printf("Sender %zd: ready\n", id);

    test_sender_ready(test);
    for (size_t cc=test->messages; cc>0; cc--)
        uio_channel_send(channel, message, message_size);

    free(message);
    return NULL;
}


static void acceptor_channel_handler(uio_acceptor_t * acceptor, uio_channel_t * channel)
{
    printf("%s -> %s: accepted\n",
        uio_channel_get_local_addr_str(channel),
        uio_channel_get_remote_addr_str(channel));

    struct test_s * test = uio_acceptor_get_extra_data(acceptor);

    uio_channel_set_input_handler(channel, &server_input_handler);
    uio_channel_set_close_handler(channel, &server_close_handler);

    test->sender_thread = malloc(sizeof(pthread_t) * test->senders);
    for (size_t idx=0; idx<test->senders; idx++)
    {
        struct sender_thread_arg_s * st_arg = malloc(sizeof(struct sender_thread_arg_s));
        st_arg->id = idx;
        st_arg->test = test;
        st_arg->channel = channel;

        int rc = pthread_create(&test->sender_thread[idx], NULL, sender_thread, st_arg);
        if (rc == 0)
            test->senders_started++;
        else
           free(st_arg);
    }
}


static void acceptor_close_handler(uio_acceptor_t * acceptor)
{
}


static void client_message_handler(uio_channel_t * channel, const void * data, size_t size)
{
    const char * ptr = ((const char*)data) + sizeof(uint16_t);
    size_t total_messages = *((uint32_t*)ptr);

    struct channel_extra_data_s * extra_data = uio_channel_get_extra_data(channel);
    if (++extra_data->messages_received == total_messages)
    {
        struct test_s * test = extra_data->test;
        struct timeval now;
        gettimeofday(&now, NULL);
        double dd = ((now.tv_sec - test->start_time.tv_sec) * 1000000
                    + (now.tv_usec - test->start_time.tv_usec));
        dd /= 1000000;
        printf("%zd senders, %zd messages, message size = %zd: %.3fs\n",
            test->senders, test->messages, test->message_size, dd);
        uio_reactor_stop(test->reactor);
    }
}


static void client_input_handler(uio_channel_t * channel, const void * data, size_t size)
{
    struct channel_extra_data_s * extra_data = uio_channel_get_extra_data(channel);
    uio_stream_defragger_process(channel, data, size, extra_data->stream_defragger, &client_message_handler);
}


static void client_close_handler(uio_channel_t * channel)
{
    printf("%s -> %s: connection closed\n",
        uio_channel_get_local_addr_str(channel),
        uio_channel_get_remote_addr_str(channel));

    struct channel_extra_data_s * extra_data = uio_channel_get_extra_data(channel);
    uio_stream_defragger_destroy(extra_data->stream_defragger);
    free(extra_data);
}


static void connector_channel_handler(uio_connector_t * connector, uio_channel_t * channel)
{
    printf("%s -> %s: connected\n",
        uio_channel_get_local_addr_str(channel),
        uio_channel_get_remote_addr_str(channel));

    struct test_s * test = uio_connector_get_extra_data(connector);
    struct channel_extra_data_s * extra_data = malloc(sizeof(struct channel_extra_data_s));
    extra_data->test = test;
    extra_data->stream_defragger = uio_stream_defragger_create(sizeof(uint16_t), &message_header_handler);
    extra_data->messages_received = 0;
    uio_channel_set_extra_data(channel, extra_data);
    uio_channel_set_input_handler(channel, &client_input_handler);
    uio_channel_set_close_handler(channel, &client_close_handler);
}


int io_send_recv_throughput()
{
    const size_t SENDERS = 1;
    const size_t MESSAGES = 1000000;
    const size_t MESSAGE_SIZE = 500;

    uio_buffer_cache_t * buffer_cache = uio_buffer_cache_create(1024*32-32, 32, 8);
    uio_thread_pool_t * thread_pool = uio_thread_pool_create(4);
    uio_reactor_t * reactor = uio_reactor_create();

    struct test_s * test = test_create(reactor, SENDERS, MESSAGES, MESSAGE_SIZE);

    uio_acceptor_t * acceptor = uio_acceptor_create(
        reactor, thread_pool, buffer_cache, /*port number*/0,
        &acceptor_channel_handler, &acceptor_close_handler);
    uio_acceptor_set_extra_data(acceptor, test);
    uio_acceptor_start(acceptor);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(uio_acceptor_get_port_number(acceptor));

    uio_connector_t * connector = uio_connector_create(
        reactor, thread_pool, buffer_cache, (struct sockaddr*)&addr, sizeof(addr), &connector_channel_handler);
    uio_connector_set_extra_data(connector, test);
    uio_connector_start(connector);

    uio_reactor_run(reactor);

    // FIXME: destroy connector
    // FIXME: destroy acceptor

    uio_reactor_destroy(reactor);
    uio_thread_pool_destroy(thread_pool, stdout);
    uio_buffer_cache_destroy(buffer_cache);

    test_destroy(test);

    return 0;
}
