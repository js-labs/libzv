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
    size_t messages;
    size_t message_size;
    struct timeval start_time;
};


static struct test_s * test_create(uio_reactor_t * reactor, size_t messages, size_t message_size)
{
    struct test_s * test = malloc(sizeof(struct test_s));
    test->reactor = reactor;
    test->messages = messages;
    test->message_size = message_size;
    return test;
}


static void test_destroy(struct test_s * test)
{
    free(test);
}


struct sender_thread_arg_s
{
    struct test_s * test;
    uio_channel_t * channel;
};


static void * sender_thread(void * _arg)
{
    struct sender_thread_arg_s * arg = _arg;
    struct test_s * test = arg->test;
    uio_channel_t * channel = arg->channel;
    free(arg);

    size_t message_size = test->message_size;
    void * message = malloc(message_size);
    char * ptr = message;
    *((uint16_t*)ptr) = (uint16_t) message_size;
    ptr += sizeof(uint16_t);
    *((uint32_t*)ptr) = (uint32_t) test->messages;

    if (message_size > (sizeof(uint16_t) + sizeof(uint32_t)))
    {
        ptr += sizeof(uint32_t);
        const size_t cc = (message_size - sizeof(uint16_t) - sizeof(uint32_t));
        for (size_t idx=0; idx<cc; idx++, ptr++)
            *ptr = (unsigned char)idx;
    }

    printf("Sender thread started...\n");
    gettimeofday(&test->start_time, NULL);
    for (size_t cc=test->messages; cc>0; cc--)
        uio_channel_send(channel, message, message_size);

    free(message);
    return NULL;
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


static void acceptor_channel_handler(uio_acceptor_t * acceptor, uio_channel_t * channel)
{
    printf("%s -> %s: accepted\n",
        uio_channel_get_local_addr_str(channel),
        uio_channel_get_remote_addr_str(channel));

    struct test_s * test = uio_acceptor_get_extra_data(acceptor);

    uio_channel_set_input_handler(channel, &server_input_handler);
    uio_channel_set_close_handler(channel, &server_close_handler);

    struct sender_thread_arg_s * st_arg = malloc(sizeof(struct sender_thread_arg_s));
    st_arg->test = test;
    st_arg->channel = channel;

    pthread_t thread_id;
    int rc = pthread_create(&thread_id, NULL, sender_thread, st_arg);
    if (rc != 0)
        free(st_arg);
}


static void acceptor_close_handler(uio_acceptor_t * acceptor)
{
    printf("Acceptor closed\n");
}


struct client_thread_arg_s
{
    struct test_s * test;
    uint16_t server_port_number;
};


static void * client_thread(void * _arg)
{
    struct client_thread_arg_s * arg = _arg;
    uio_socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(arg->server_port_number);

        int rc = connect(s, (const struct sockaddr*) &addr, sizeof(addr));
        if (rc == 0)
        {
            printf("Connected to server\n");

            size_t bytes_received = 0;
            size_t bytes_expected = 0;
            char buffer[1024*64];

            for (;;)
            {
                rc = uio_socket_read(s, buffer, sizeof(buffer));
                if (rc > 0)
                {
                    if (bytes_received == 0)
                    {
                        const void * ptr = buffer;
                        size_t message_size = *((const uint16_t*)ptr);
                        ptr = ((const char*)ptr) + sizeof(uint16_t);
                        size_t messages = *((const uint32_t*)ptr);
                        bytes_expected = (message_size * messages);
                    }
                    bytes_received += rc;
                    if (bytes_received == bytes_expected)
                    {
                        struct test_s * test = arg->test;
                        struct timeval now;
                        gettimeofday(&now, NULL);
                        double dd = ((now.tv_sec - test->start_time.tv_sec) * 1000000
                            + (now.tv_usec - test->start_time.tv_usec));
                        dd /= 1000000;
                        printf("%zd messages, message size = %zd: %.3fs\n", test->messages, test->message_size, dd);
                        uio_reactor_stop(test->reactor);
                    }
                }
                else if (rc == 0)
                {
                    printf("Connection to server closed\n");
                    break;
                }
                else
                {
                    printf("read() failed\n");
                    break;
                }
            }
        }
        else
            printf("connect() failed\n");

        uio_socket_close(s);
    }
    else
        printf("socket() failed\n");

    free(arg);
    return NULL;
}


int io_send_throughput()
{
    const size_t MESSAGES = 1000000;
    const size_t MESSAGE_SIZE = 500;

    uio_buffer_cache_t * buffer_cache = uio_buffer_cache_create(1024*32-32, 32, 8);
    uio_thread_pool_t * thread_pool = uio_thread_pool_create(4);
    uio_reactor_t * reactor = uio_reactor_create();

    struct test_s * test = test_create(reactor, MESSAGES, MESSAGE_SIZE);

    uio_acceptor_t * acceptor = uio_acceptor_create(
        reactor, thread_pool, buffer_cache, /*port number*/0,
        &acceptor_channel_handler, &acceptor_close_handler);
    uio_acceptor_set_extra_data(acceptor, test);
    uio_acceptor_start(acceptor);

    struct client_thread_arg_s * client_thread_arg = malloc(sizeof(struct client_thread_arg_s));
    client_thread_arg->test = test;
    client_thread_arg->server_port_number = uio_acceptor_get_port_number(acceptor);

    pthread_t client_thread_id;
    int rc = pthread_create(&client_thread_id, NULL, client_thread, client_thread_arg);
    if (rc == 0)
        uio_reactor_run(reactor);
    else
        free(client_thread_arg);

    // FIXME: destroy connector
    // FIXME: destroy acceptor

    uio_reactor_destroy(reactor);
    uio_thread_pool_destroy(thread_pool, stdout);
    uio_buffer_cache_destroy(buffer_cache);

    test_destroy(test);

    return 0;
}
