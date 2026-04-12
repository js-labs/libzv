#include "zv/os_socket.h"
#include "zv/os_thread.h"
#include "ss_blackhole_server.h"
#include "helpers.h"
#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#define IOV_MAX 512
static inline WSABUF make_iovec(char * ptr, size_t len)
{
    WSABUF ret = { .buf = ptr, .len = (ULONG)len };
    return ret;
}
#else
static inline struct iovec make_iovec(char * ptr, size_t len)
{
    struct iovec ret = { .iov_base = ptr, .iov_len = len };
    return ret;
}
#endif


static long run_test(
    in_port_t server_port,
    size_t total_bytes,
    size_t block_size
) {
    zv_socket_t sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET)
    {
        fprintf(stderr, "socket() failed\n");
        return -1;
    }

    long ret = -1;
    struct sockaddr_in addr = make_sockaddr_in(INADDR_LOOPBACK, server_port);
    int rc = connect(sockfd, (const struct sockaddr*)&addr, sizeof(addr));
    if (rc == 0)
    {
#if defined(_WIN32)
        WSABUF iov[IOV_MAX];
#else
        struct iov iovec[IOV_MAX];
#endif
        char * buffer = malloc(IOV_MAX * block_size);
        if (buffer != NULL)
        {
            for (size_t idx=0; idx<IOV_MAX; idx++)
                iov[idx] = make_iovec(buffer + idx*block_size, block_size);

            size_t bytes_remaining = total_bytes;
            struct timespec ts_start;
            clock_gettime(CLOCK_MONOTONIC, &ts_start);

            for (;;)
            {
                int iovc;
                if ((IOV_MAX * block_size) <= bytes_remaining)
                    iovc = IOV_MAX;
                else
                    iovc = (bytes_remaining / block_size);

#if defined(_WIN32)
                DWORD bytes_sent;
                rc = WSASend(sockfd, iov, iovc, &bytes_sent, 0, NULL, NULL);
                if (rc != 0)
                {
                    fprintf(stderr, "WSASend() failed\n");
                    break;
                }
#else
                ssize_t bytes_sent = writev(sockfd, iov, iovc);
                if (bytes_sent < 0)
                {
                    fprintf(stderr, "writev() failed\n");
                    break;
                }
#endif
                if (bytes_sent != (iovc * block_size))
                {
                    fprintf(
                        stderr,
                        "internal error: bytes_sent=%u instead of %zu\n",
                        bytes_sent,
                        (iovc * block_size)
                    );
                    break;
                }

                if (bytes_sent >= bytes_remaining)
                    break;
                bytes_remaining -= bytes_sent;
            }

            struct timespec ts_end;
            clock_gettime(CLOCK_MONOTONIC, &ts_end);

            ret = zv_elapsed_time_us(&ts_start, &ts_end) / 1000;

            free(buffer);
        }
        else
            fprintf(stderr, "malloc() failed\n");
    }
    else
        fprintf(stderr, "connect() failed\n");

    zv_socket_close(sockfd);
    return ret;
}


int main(int argc, const char * argv[])
{
    int rc;

#if defined(_WIN32)
    rc = wsa_init();
    if (rc != 0)
    {
        fprintf(stderr, "wsa_init() failed\n");
        return -1;
    }
#endif

    in_port_t server_port;
    ss_blackhole_server_t * server = ss_blackhole_server_create(&server_port);
    if (server == NULL)
    {
        fprintf(stderr, "ss_blackhole_server_create() failed\n");
        return -1;
    }

    size_t total_bytes = 100 * 1024 * 1024;
    size_t block_size = 16;
    for (size_t idx=0; idx<13; idx++)
    {
        long elapsed_time_ms = run_test(server_port, total_bytes, block_size);
        printf(
            "sent %zu bytes with block size=%zu @ %ld.%.3ld sec\n",
            total_bytes,
            block_size,
            (elapsed_time_ms / 1000),
            (elapsed_time_ms % 1000)
        );
        block_size *= 2;
    }

    ss_blackhole_server_stop_and_destroy(server);

#if defined(_WIN32)
    WSACleanup();
#endif

    return 0;
}
