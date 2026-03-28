#include "zv/acceptor.h"
#include "zv/array.h"
#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/os_compat.h"
#include "helpers.h"
#include "tcp_echo_server.h"

#if defined(_WIN32)
#include <ws2ipdef.h>
#else
#include <sys/fcntl.h>
#include <sys/select.h>
#include <unistd.h>
#endif
#include <assert.h>
#include <errno.h>

struct tcp_echo_server_zv_s
{
    zv_loop_t * loop;
    zv_buffer_cache_t * buffer_cache;
    zv_acceptor_t * acceptor;
};


struct tcp_echo_server_sock_s
{
    zv_socket_t cmd_pipe[2];
    zv_socket_t accept_fd;
};


struct tcp_echo_server_s
{
    int use_libxv;
    pthread_t thread;

    union
    {
        struct tcp_echo_server_zv_s xv;
        struct tcp_echo_server_sock_s sock;
    };
};


static void tcp_echo_server_input_handler(
    zv_channel_t * channel,
    const void * data,
    size_t size
) {
    zv_channel_send_sync(channel, data, size);
}


static void tcp_echo_server_close_handler(zv_channel_t * channel)
{
    /*
    printf(
        "%s -> %s: connection closed\n",
        zv_channel_get_local_addr_str(channel),
        zv_channel_get_peer_addr_str(channel)
    );
    */
}


static void tcp_echo_server_accept_handler(zv_acceptor_t * acceptor, zv_socket_t sockfd)
{
    struct tcp_echo_server_zv_s * server = zv_acceptor_get_extra_data(acceptor);
    zv_channel_t * channel = zv_channel_create(
        server->loop,
        server->buffer_cache,
        sockfd,
        &tcp_echo_server_input_handler,
        &tcp_echo_server_close_handler,
        server
    );

    if (channel == NULL)
    {
        fprintf(stderr, "zv_channel_create() failed\n");
        zv_socket_close(sockfd);
    }
    else
    {
        /*
        printf(
            "%s -> %s: connection accepted\n",
            zv_channel_get_local_addr_str(channel),
            zv_channel_get_peer_addr_str(channel)
        );
        */
        zv_channel_start(channel);
        zv_channel_release(channel);
    }
}


static void * tcp_echo_server_zv_loop_thread_func(void * arg)
{
    struct tcp_echo_server_zv_s * server = arg;
    zv_loop_run(server->loop);
    zv_acceptor_destroy(server->acceptor);
    zv_buffer_cache_release(server->buffer_cache);
    zv_loop_destroy(server->loop);
    return NULL;
}


struct sock_client_s
{
    zv_socket_t sockfd;
    char local_addr_str[INET_ADDRSTRLEN];
    char peer_addr_str[INET_ADDRSTRLEN];
};


static void * tcp_echo_server_sock_loop_thread_func(void * arg)
{
    struct tcp_echo_server_sock_s * server = arg;
    zv_socket_t cmd_fd = server->cmd_pipe[0];
    struct sock_client_s* clients = NULL;
    size_t buffer_size = (16 * 1024);
    void * buffer = malloc(buffer_size);
    fd_set read_fds;
    fd_set write_fds;
    fd_set except_fds;

    for (;;)
    {
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_ZERO(&except_fds);

        FD_SET(cmd_fd, &read_fds);
        FD_SET(server->accept_fd, &read_fds);

#if defined(_WIN32)
        int nfds = 0;
        for (size_t idx=0; idx<zv_array_get_size(clients); idx++)
        {
            struct sock_client_s * client = &clients[idx];
            FD_SET(client->sockfd, &read_fds);
        }
#else
        int max_fd = cmd_fd;
        if (server->accept_fd > max_fd)
            max_fd = server->accept_fd;

        for (size_t idx=0; idx<zv_array_get_size(clients); idx++)
        {
            struct sock_client_s * client = &clients[idx];
            FD_SET(client->sockfd, &read_fds);
            if (client->sockfd > max_fd)
                max_fd = client->sockfd;
        }
        int nfds = (max_fd + 1);
#endif

        int fds_ready = select(nfds, &read_fds, &write_fds, &except_fds, NULL);
        if (FD_ISSET(cmd_fd, &read_fds))
            break;

        for (size_t idx=0; idx<zv_array_get_size(clients);)
        {
            struct sock_client_s * client = &clients[idx];
            if (FD_ISSET(client->sockfd, &read_fds))
            {
                ssize_t bytes_received = recv(client->sockfd, buffer, (int)buffer_size, 0);
                if (bytes_received > 0)
                {
                    ssize_t bytes_sent = send(client->sockfd, buffer, bytes_received, 0);
                    if (bytes_sent < 0)
                    {
                        printf(
                            "%s -> %s: failed to send data\n",
                            client->local_addr_str,
                            client->peer_addr_str
                        );
                    }
                    idx++;
                }
                else if (bytes_received == 0)
                {
                    /*
                    printf(
                        "%s -> %s: connection closed\n",
                        client->local_addr_str,
                        client->peer_addr_str
                    );
                    */
#if defined(_WIN32)
                    closesocket(client->sockfd);
#else
                    close(client->sockfd);
#endif
                    zv_array_remove_at(clients, idx);
                }
            }
            else
                idx++;
        }

        if (FD_ISSET(server->accept_fd, &read_fds))
        {
            struct sockaddr_in peer_addr;
            socklen_t peer_addrlen = sizeof(peer_addr);
            zv_socket_t sockfd = accept(server->accept_fd, (struct sockaddr*) &peer_addr, &peer_addrlen);
            if (sockfd == -1)
                fprintf(stderr, "accept() failed\n");
#if defined(_WIN32)
            else if ((zv_array_get_size(clients) + 2) == FD_SETSIZE)
            {
                fprintf(
                    stderr,
                    "reached maximum number of sockets (%d) for fd_set, closing connection.\n",
                    FD_SETSIZE
                );
                zv_socket_close(sockfd);
            }
#else
            else if (sockfd >= FD_SETSIZE)
            {
                fprintf(
                    stderr,
                    "accepted client socket %d is greater than maximum possible "
                    "(%d) for 'fd_set', closing connection\n",
                    sockfd,
                    FD_SETSIZE
                );
                abort();
            }
#endif
            else
            {
                struct sockaddr_in local_addr;
                socklen_t local_addrlen = sizeof(peer_addr);
                if (getsockname(sockfd, (struct sockaddr*) &local_addr, &local_addrlen) == 0)
                {
                    struct sock_client_s * client = zv_array_append(clients);
                    client->sockfd = sockfd;
                    zv_inet_ntop(
                        (const struct sockaddr*) &local_addr,
                        sizeof(local_addr),
                        client->local_addr_str,
                        sizeof(client->local_addr_str)
                    );
                    zv_inet_ntop(
                        (const struct sockaddr*) &peer_addr,
                        sizeof(peer_addr),
                        client->peer_addr_str,
                        sizeof(client->peer_addr_str)
                    );
                    /*
                    printf(
                        "%s -> %s: connection accepted\n",
                        client->local_addr_str,
                        client->peer_addr_str
                    );
                    */
                }
                else
                {
                    fprintf(stderr, "getsockname() failed, closing connection\n");
                    zv_socket_close(sockfd);
                }
            }
        }
    }

#if defined(_WIN32)
    closesocket(server->cmd_pipe[0]);
    closesocket(server->cmd_pipe[1]);
    closesocket(server->accept_fd);
#else
    close(server->cmd_pipe[0]);
    close(server->cmd_pipe[1]);
    close(server->accept_fd);
#endif

    for (size_t idx=0; idx<zv_array_get_size(clients); idx++)
    {
        struct sock_client_s * client = &clients[idx];
        zv_socket_close(client->sockfd);
    }

    free(buffer);
    zv_array_destroy(clients);

    printf("echo server stopped\n");
    return NULL;
}


static int tcp_echo_server_zv_init(
    struct tcp_echo_server_s * server,
    struct zv_loop_config_s * loop_config,
    in_port_t * port
) {
    zv_loop_t * loop = zv_loop_create(loop_config);
    if (loop == NULL)
        return -1;

    zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(1024*8, 8, 2);
    if (buffer_cache)
    {
        struct sockaddr_in addr = make_sockaddr_in(INADDR_ANY, 0);
        zv_acceptor_t * acceptor = zv_acceptor_create(
            loop,
            (const struct sockaddr*) &addr,
            sizeof(addr),
            tcp_echo_server_accept_handler,
            &server->xv
        );

        if (acceptor != NULL)
        {
            *port = zv_acceptor_get_port(acceptor);
            server->xv.loop = loop;
            server->xv.buffer_cache = buffer_cache;
            server->xv.acceptor = acceptor;

            int rc = pthread_create(
                &server->thread,
                NULL,
                &tcp_echo_server_zv_loop_thread_func,
                &server->xv
            );

            if (rc == 0)
            {
                zv_acceptor_start(acceptor);
                printf(
                    "TCP echo server (XV/%s) started on port %hu\n",
                    zv_loop_type_get_name(zv_loop_get_type(loop)),
                    *port
                );

                return 0;
            }

            zv_acceptor_destroy(acceptor);
        }
        else
            fprintf(stderr, "zv_acceptor_create() failed\n");

        zv_buffer_cache_release(buffer_cache);
    }
    else
        fprintf(stderr, "zv_buffer_cache_create() failed\n");

    zv_loop_destroy(loop);
    return -1;
}


static int tcp_echo_server_sock_init(
    struct tcp_echo_server_s * server,
    in_port_t * port
) {
    zv_socket_t cmd_pipe[2];
    int rc = zv_pipe(cmd_pipe);
    if (rc != 0)
    {
        fprintf(stderr, "zv_pipe() failed\n");
        return rc;
    }

    zv_socket_t sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd != INVALID_SOCKET)
    {
        struct sockaddr_in addr = make_sockaddr_in(INADDR_ANY, 0);
        rc = bind(sockfd, (struct sockaddr*) &addr, sizeof(addr));
        if (rc == 0)
        {
            socklen_t namelen = sizeof(addr);
            rc = getsockname(sockfd, (struct sockaddr*) &addr, &namelen);
            if (rc == 0)
            {
        #if defined(_WIN32)
                u_long mode = 1;
                rc = ioctlsocket(sockfd, FIONBIO, &mode);
        #else
                rc = fcntl(sockfd, F_SETFL, O_NONBLOCK);
        #endif
                if (rc == 0)
                {
                    rc = listen(sockfd, 10);
                    if (rc == 0)
                    {
                        *port = htons(addr.sin_port);
                        server->sock.cmd_pipe[0] = cmd_pipe[0];
                        server->sock.cmd_pipe[1] = cmd_pipe[1];
                        server->sock.accept_fd = sockfd;

                        printf("echo server (SOCK) started on port %hu\n", *port);

                        rc = pthread_create(
                            &server->thread,
                            NULL,
                            &tcp_echo_server_sock_loop_thread_func,
                            &server->sock
                        );
                        if (rc == 0)
                            return rc;
                        else
                            printf("pthread_create() failed\n");
                    }
                    else
                        printf("listen() failed\n");
                }
                else
                    printf("fcntl() failed\n");
            }
            else
                printf("getsockname() failed\n");
        }
        else
            printf("bind() failed\n");

        zv_socket_close(sockfd);
    }
    else
        printf("socket() failed\n");

    zv_socket_close(cmd_pipe[0]);
    zv_socket_close(cmd_pipe[1]);
    return -1;
}


tcp_echo_server_t * tcp_echo_server_create(
    struct zv_loop_config_s * loop_config,
    in_port_t * port
) {
    struct tcp_echo_server_s * server = malloc(sizeof(struct tcp_echo_server_s));
    if (server == NULL)
        return NULL;

    int rc;
    if (loop_config == NULL)
    {
        server->use_libxv = 0;
        rc = tcp_echo_server_sock_init(server, port);
    }
    else
    {
        server->use_libxv = 1;
        rc = tcp_echo_server_zv_init(server, loop_config, port);
    }

    if (rc == 0)
        return server;
    else
    {
        free(server);
        return NULL;
    }
}


void tcp_echo_server_stop_and_destroy(tcp_echo_server_t * server)
{
    if (server->use_libxv)
        zv_loop_stop(server->xv.loop);
    else
    {
        char cmd = 0;
    #if defined(_WIN32)
        send(server->sock.cmd_pipe[1], &cmd, sizeof(cmd), 0);
    #else
        ssize_t rc = write(server->sock.cmd_pipe[1], &cmd, sizeof(cmd));
        assert(rc == sizeof(cmd));
        (void)(rc);
    #endif
    }

    pthread_join(server->thread, NULL);
    free(server);
}
