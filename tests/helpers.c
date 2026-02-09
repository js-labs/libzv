/*
 * Copyright (C) 2022 Sergey Zubarev, info@js-labs.org
 *
 * This file is a part of ZV framework.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <stdio.h>
#include "zv/buffer.h"
#include "zv/thread_pool.h"
#include "helpers.h"

#if defined(_WIN32)
#include <time.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

struct sockaddr_in make_sockaddr_in(in_addr_t in_addr, in_port_t in_port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(in_port);
    addr.sin_addr.s_addr = htonl(in_addr);
    return addr;
}

int get_sock_names(
    zv_socket_t sockfd,
    char * local_addr_str, int local_addr_str_len,
    char * peer_addr_str, int peer_addr_str_len
) {
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    const char * ptr;
    int rc;

    rc = getsockname(sockfd, (struct sockaddr*) &addr, &addrlen);
    if (rc != 0)
        return rc;

    ptr = zv_inet_ntop(
        (const struct sockaddr*) &addr,
        addrlen,
        local_addr_str,
        local_addr_str_len
    );
    if (!ptr)
        return -1;

    addrlen = sizeof(addr);
    rc = getpeername(sockfd, (struct sockaddr*) &addr, &addrlen);
    if (rc != 0)
        return rc;

    ptr = zv_inet_ntop(
        (const struct sockaddr*) &addr,
        addrlen,
        peer_addr_str,
        peer_addr_str_len
    );
    if (!ptr)
        return -1;

    return 0;
}

#if !defined(_WIN32)
int closesocket(zv_socket_t sockfd)
{
    return close(sockfd);
}
#endif

struct loop_thread_arg_s
{
    zv_loop_t * loop;
    zv_acceptor_t * acceptor;
    void (*after_acceptor_stop)(zv_acceptor_t*);
};


static void * loop_thread_func(void * _arg)
{
    struct loop_thread_arg_s * arg = (struct loop_thread_arg_s*) _arg;
    zv_loop_t * loop = arg->loop;
    zv_acceptor_t * acceptor = arg->acceptor;
    void (*after_acceptor_stop)(zv_acceptor_t*) = arg->after_acceptor_stop;
    free(arg);
    zv_loop_run(loop);
    after_acceptor_stop(acceptor);
    zv_acceptor_destroy(acceptor);
    zv_loop_destroy(loop);
    return NULL;
}


static void * loop_thread_func1(void * arg)
{
    zv_loop_t * loop = (zv_loop_t*) arg;
    zv_loop_run(loop);
    zv_loop_destroy(loop);
    return NULL;
}


zv_loop_t * start_loop(
    pthread_t * loop_thread,
    const struct zv_loop_config_s * loop_config
) {
    zv_loop_t * loop = zv_loop_create(loop_config);
    if (loop)
    {
        int rc = pthread_create(loop_thread, NULL, &loop_thread_func1, loop);
        if (rc == 0)
            return loop;
        else
            fprintf(stderr, "pthread_create() failed\n");
        zv_loop_destroy(loop);
    }
    else
        fprintf(stderr, "zv_loop_create() failed\n");
    return NULL;
}


zv_acceptor_t * start_acceptor_with_buffer_cache(
    zv_loop_t * loop,
    void (*accept_handler)(zv_acceptor_t*, zv_socket_t)
) {
    zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(1024*8, 16, 2);
    if (buffer_cache)
    {
        struct sockaddr_in addr = make_sockaddr_in(INADDR_ANY, 0);
        zv_acceptor_t * acceptor = zv_acceptor_create(
            loop,
            (const struct sockaddr*) &addr,
            sizeof(addr),
            accept_handler,
            buffer_cache
        );

        if (acceptor)
        {
            zv_acceptor_start(acceptor);
            return acceptor;
        }

        printf("zv_acceptor_create() failed\n");
        zv_buffer_cache_release(buffer_cache);
    }
    return NULL;
}


static void after_connect_fail(zv_connector_t * connector, int sockerr)
{
    // should not happen
    abort();
}


zv_loop_t * create_loop_and_connector(
    in_port_t port,
    void (*after_connect)(zv_connector_t*, zv_socket_t)
) {
    zv_loop_t * loop = zv_loop_create(NULL);
    if (!loop)
    {
        printf("zv_loop_create() failed\n");
        return NULL;
    }

    struct sockaddr_in addr = make_sockaddr_in(INADDR_LOOPBACK, port);
    zv_connector_t * connector = zv_connector_create(
        loop,
        (struct sockaddr*) &addr,
        sizeof(addr),
        after_connect,
        after_connect_fail,
        NULL
    );

    if (connector)
    {
        zv_connector_start(connector);
        return loop;
    }
    else
        printf("zv_connector_create() failed\n");

    zv_loop_destroy(loop);
    return NULL;
}


struct sock_server_thread_arg_s
{
    void (*func)(zv_socket_t);
    zv_socket_t accept_socket;
};


static void * sock_server_thread_func(void * _arg)
{
    struct sock_server_thread_arg_s * arg = (struct sock_server_thread_arg_s*) _arg;
    void (*func)(zv_socket_t) = arg->func;
    zv_socket_t accept_socket = arg->accept_socket;
    free(arg);
    func(accept_socket);
    return NULL;
}


int start_sock_server(
    in_port_t * port,
    pthread_t * thread,
    void (*func)(zv_socket_t accept_socket)
) {
    zv_socket_t sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET)
        return -1;

    struct sockaddr_in addr = make_sockaddr_in(INADDR_ANY, 0);
    int rc = bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
    if (rc == 0)
    {
        socklen_t namelen = sizeof(addr);
        rc = getsockname(sockfd, (struct sockaddr*)&addr, &namelen);
        if (rc == 0)
        {
            *port = ntohs(addr.sin_port);
            rc = listen(sockfd, 1);
            if (rc == 0)
            {
                struct sock_server_thread_arg_s * thread_arg = malloc(sizeof(struct sock_server_thread_arg_s));
                if (thread_arg)
                {
                    thread_arg->func = func;
                    thread_arg->accept_socket = sockfd;
                    rc = pthread_create(thread, NULL, &sock_server_thread_func, thread_arg);
                    if (rc == 0)
                        return 0;
                    free(thread_arg);
                }
            }
            else
                printf("listen() failed\n");
        }
        else
            printf("getsockname() failed\n");
    }
    closesocket(sockfd);
    return rc;
}


zv_socket_t sock_connect(in_addr_t in_addr, in_port_t in_port)
{
    zv_socket_t sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd != INVALID_SOCKET)
    {
        struct sockaddr_in addr = make_sockaddr_in(in_addr, in_port);
        int rc = connect(sockfd, (const struct sockaddr*) &addr, sizeof(addr));
        if (rc == 0)
            return sockfd;
        else
            printf("connect() failed\n");
        closesocket(sockfd);
    }
    else
        printf("socket() failed\n");
    return INVALID_SOCKET;
}


int run_timer_pool_test(int (*test_func)(zv_timer_pool_t*))
{
    int rc = -1;
    zv_thread_pool_t * thread_pool = zv_thread_pool_create(2);
    if (thread_pool)
    {
        zv_timer_pool_t * timer_pool = zv_timer_pool_create(thread_pool, NULL);
        if (timer_pool)
        {
            rc = test_func(timer_pool);
            zv_timer_pool_destroy(timer_pool);
        }
        zv_thread_pool_destroy(thread_pool);
    }
    return rc;
}


uint32_t timeval_elapsed_time(const struct timeval * start, const struct timeval * end)
{
    // (end->tv_sec * C + end->tv_usec) - (start->tv_sec * C + start->tv_usec)
    uint64_t ret = end->tv_sec;
    ret -= start->tv_sec;
    ret *= (1000 * 1000);
    ret += end->tv_usec;
    ret -= start->tv_usec;
    return (uint32_t) ret;
}


void timeval_add_ms(struct timeval * tv, unsigned int ms)
{
    const long us_in_sec = (1000 * 1000);
    tv->tv_usec += (ms * 1000);
    if (tv->tv_usec >= us_in_sec)
    {
        tv->tv_sec += (tv->tv_usec / us_in_sec);
        tv->tv_usec %= us_in_sec;
    }
}


void timespec_add_ms(struct timespec * ts, unsigned int ms)
{
    const long ns_in_sec = (1000 * 1000 * 1000);
    ts->tv_nsec += (ms * 1000 * 1000);
    if (ts->tv_nsec >= ns_in_sec)
    {
        ts->tv_sec += (ts->tv_nsec / ns_in_sec);
        ts->tv_nsec %= ns_in_sec;
    }
}


void timespec_now_adding_ms(struct timespec * ts, unsigned int ms)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    timeval_add_ms(&tv, ms);
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = (tv.tv_usec * 1000);
}
