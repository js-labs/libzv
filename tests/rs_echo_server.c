#include "rs_echo_server.h"
#include "rs_server.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#if defined(_WIN32)
#define IOV_MAX 1024
#define iovec _WSABUF 
#else
#include <limits.h>
#include <string.h>
#include <sys/uio.h>
#endif


static inline struct iovec make_iovec(char * ptr, size_t len)
{
#if defined(_WIN32)
    WSABUF ret = { .buf = ptr, .len = (ULONG)len };
#else
    struct iovec ret = { .iov_base = ptr, .iov_len = len };
#endif
    return ret;
}


struct data_block_s
{
    struct data_block_s * next;
    void * data;
    size_t size;
};


struct extra_data_s
{
    size_t bytes_received;
    struct data_block_s * sq_head;
    struct data_block_s * sq_tail;
};


static void _rs_echo_server_accept_handler(struct ss_client_s * client)
{
    struct extra_data_s * extra_data = malloc(sizeof(*extra_data));
    if (extra_data == NULL)
    {
        fprintf(stderr, "malloc() failed\n");
        abort();
    }
    extra_data->bytes_received = 0;
    extra_data->sq_head = NULL;
    extra_data->sq_tail = NULL;
    client->extra_data = extra_data;
}


static void _rs_echo_server_input_handler(
    struct ss_client_s * client,
    const void * data,
    size_t size
) {
    struct extra_data_s * extra_data = client->extra_data;
    extra_data->bytes_received += size;

    if (client->wait_wr > 0)
    {
        assert(extra_data->sq_tail != NULL);
        struct data_block_s * data_block = malloc(sizeof(struct data_block_s) + size);
        if (data_block == NULL)
        {
            fprintf(stderr, "malloc() failed\n");
            abort();
        }
        data_block->next = NULL;
        data_block->data = (data_block + 1);
        data_block->size = size;
        memcpy(data_block->data, data, size);
        extra_data->sq_tail->next = data_block;
        extra_data->sq_tail = data_block;
    }
    else if (client->wait_wr == 0)
    {
        ssize_t bytes_sent = send(client->sockfd, data, size, 0);
        if ((bytes_sent >= 0) && (((size_t)bytes_sent) < size))
        {
            size_t bytes_remaining = (size - bytes_sent);
            client->wait_wr = 1;
            struct data_block_s * data_block = malloc(sizeof(*data_block) + bytes_remaining);
            if (data_block == NULL)
            {
                fprintf(stderr, "malloc() failed\n");
                abort();
            }
            data_block->next = NULL;
            data_block->data = (data_block + 1);
            data_block->size = size;
            memcpy(data_block->data, ((const char*)data) + bytes_sent, bytes_remaining);
            extra_data->sq_head = data_block;
            extra_data->sq_tail = data_block;
        }
    }
    else if (client->wait_wr < 0)
    {
        // last write operation failed,
        // most probably connection is closed
    }
}


void _rs_echo_server_write_handler(struct ss_client_s * client)
{
    struct extra_data_s * extra_data = client->extra_data;
    struct data_block_s * data_block = extra_data->sq_head;
    struct iovec iov[IOV_MAX];
    int iovc = 0;
    do
    {
        iov[iovc] = make_iovec(data_block->data, data_block->size);
        if (++iovc == IOV_MAX)
            break;
        data_block = data_block->next;
    }
    while (data_block != NULL);

#if defined(_WIN32)
    DWORD dw_bytes_sent;
    int rc = WSASend(client->sockfd, iov, iovc, &dw_bytes_sent, 0, NULL, NULL);
    ssize_t send_rc = (rc > 0) ? dw_bytes_sent : -1;
#else
    ssize_t send_rc = writev(client->sockfd, iov, iovc);
#endif

    if (send_rc < 0)
    {
        client->wait_wr = -1;
        return;
    }
    else if (send_rc == 0)
        return;

    size_t bytes_sent = send_rc;
    data_block = extra_data->sq_head;
    for (;;)
    {
        if (bytes_sent > data_block->size)
        {
            struct data_block_s * next = data_block->next;
            free(data_block);
            data_block = next;
        }
        else if (bytes_sent == data_block->size)
        {
            struct data_block_s * next = data_block->next;
            free(data_block);
            extra_data->sq_head = next;
            if (next == NULL)
            {
                extra_data->sq_tail = NULL;
                client->wait_wr = 0;
            }
            break;
        }
        else // (bytes_sent < data_block->size
        {
            data_block->data = ((char*)data_block->data) + bytes_sent;
            data_block->size = (data_block->size - bytes_sent);
            extra_data->sq_head = data_block;
            break;
        }
    }
}


void _rs_echo_server_close_handler(struct ss_client_s * client)
{
    struct extra_data_s * extra_data = client->extra_data;
    // printf("received %zu bytes\n", extra_data->bytes_received);
    struct data_block_s * data_block = extra_data->sq_head;
    while (data_block != NULL)
    {
        struct data_block_s * next = data_block->next;
        free(data_block);
        data_block = next;
    }
    free(extra_data);
}


rs_echo_server_t * rs_echo_server_create(in_port_t * port)
{
    rs_server_t * server = rs_server_create(
        port,
        &_rs_echo_server_accept_handler,
        &_rs_echo_server_input_handler,
        &_rs_echo_server_write_handler,
        &_rs_echo_server_close_handler
    );
    if (server != NULL)
        printf("TCP echo server (RS) started on port %hu\n", *port);
    return server;
}


void rs_echo_server_stop_and_destroy(rs_echo_server_t * server)
{
    rs_server_stop_and_destroy(server);
}
