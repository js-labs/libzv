#include "zv/os_socket.h"
#include "zv/os_thread.h"
#include "rs_blackhole_server.h"
#include "helpers.h"
#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#define IOV_MAX 1024
#define iovec _WSABUF
#else
#include <limits.h>
#include <stdlib.h>
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

static long run_test(
    in_port_t server_port,
    size_t total_bytes,
    size_t block_size,
    size_t iovc_max
) {
    zv_socket_t sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET)
    {
        fprintf(stderr, "socket() failed\n");
        return -1;
    }

    struct iovec * iov = malloc(sizeof(struct iovec) * iovc_max);
    if (iov == NULL)
    {
        fprintf(stderr, "malloc() failed\n");
        zv_socket_close(sockfd);
        return -1;
    }

    long ret = -1;
    struct sockaddr_in addr = make_sockaddr_in(INADDR_LOOPBACK, server_port);
    int rc = connect(sockfd, (const struct sockaddr*)&addr, sizeof(addr));
    if (rc == 0)
    {
        char * buffer = malloc(iovc_max * block_size);
        if (buffer != NULL)
        {
            for (size_t idx=0; idx<iovc_max; idx++)
                iov[idx] = make_iovec(buffer + idx*block_size, block_size);

            size_t bytes_remaining = total_bytes;
            struct timespec ts_start;
            clock_gettime(CLOCK_MONOTONIC, &ts_start);

            for (;;)
            {
                int iovc;
                if ((iovc_max * block_size) <= bytes_remaining)
                    iovc = iovc_max;
                else
                    iovc = (int) (bytes_remaining / block_size);

#if defined(_WIN32)
                DWORD dw_bytes_sent;
                rc = WSASend(sockfd, iov, iovc, &dw_bytes_sent, 0, NULL, NULL);
                if (rc != 0)
                {
                    fprintf(stderr, "WSASend() failed: %d\n", WSAGetLastError());
                    break;
                }
                ssize_t bytes_sent = dw_bytes_sent;
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
                        "internal error: bytes_sent=%zd instead of %zu\n",
                        bytes_sent,
                        (block_size * iovc)
                    );
                    break;
                }

                if (((size_t)bytes_sent) >= bytes_remaining)
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
#if defined(_WIN32)
    if (wsa_init() != 0)
    {
        fprintf(stderr, "wsa_init() failed\n");
        return -1;
    }
#endif

    in_port_t server_port;
    rs_blackhole_server_t * server = rs_blackhole_server_create(&server_port);
    if (server == NULL)
    {
        fprintf(stderr, "ss_blackhole_server_create() failed\n");
        return -1;
    }

    size_t total_bytes = 100 * 1024 * 1024;
    size_t block_size = 16;
    printf("Send %zu MB...\n", (total_bytes) / (1024*1024));
    printf("       |");
    for (size_t iovc_max=IOV_MAX; iovc_max>128; iovc_max/=2)
        printf(" %6zu |", iovc_max);
    printf("\n-------+--------+--------+--------+\n");

    for (size_t idx=0; idx<13; idx++)
    {
        printf("%6zu |", block_size);
        for (size_t iovc_max=IOV_MAX; iovc_max>128; iovc_max/=2)
        {
            long elapsed_time_ms = run_test(server_port, total_bytes, block_size, iovc_max);
            printf(" %2ld.%.3ld |", (elapsed_time_ms / 1000), (elapsed_time_ms % 1000));
        }
        printf("\n");
        block_size *= 2;
    }

    rs_blackhole_server_stop_and_destroy(server);

#if defined(_WIN32)
    WSACleanup();
#endif

    return 0;
}
