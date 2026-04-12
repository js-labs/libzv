#include "ss_server.h"
#include "helpers.h"
#include "zv/os_thread.h"
#include "zv/array.h"
#include <assert.h>
#include <stdio.h>

struct ss_server_s
{
    void (*accept_handler)(struct ss_client_s*);
    void (*input_handler)(struct ss_client_s*, const void*, size_t);
    void (*write_handler)(struct ss_client_s*);
    void (*close_handler)(struct ss_client_s*);
    zv_socket_t cmd_pipe[2];
    zv_socket_t accept_fd;
    pthread_t thread;
};


static void * _ss_server_thread_func(void * arg)
{
    struct ss_server_s * server = arg;
    struct ss_client_s * clients = NULL;
    size_t buffer_size = (64 * 1024);
    void * buffer = malloc(buffer_size);
    fd_set read_fds;
    fd_set write_fds;
    fd_set except_fds;

    for (;;)
    {
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_ZERO(&except_fds);

        FD_SET(server->cmd_pipe[0], &read_fds);
        FD_SET(server->accept_fd, &read_fds);

#if defined(_WIN32)
        int nfds = 0;
        for (size_t idx=0; idx<zv_array_get_size(clients); idx++)
        {
            struct ss_client_s * client = &clients[idx];
            FD_SET(client->sockfd, &read_fds);
            if (client->wait_wr)
                FD_SET(client->sockfd, &write_fds);
        }
#else
        int max_fd = server->cmd_pipe[0];
        if (server->accept_fd > max_fd)
            max_fd = server->accept_fd;

        for (size_t idx=0; idx<zv_array_get_size(clients); idx++)
        {
            struct s_client_s * client = &clients[idx];
            FD_SET(client->sockfd, &read_fds);
            if (client->wait_wr)
                FD_SET(client->sockfd, &write_fds);
            if (client->sockfd > max_fd)
                max_fd = client->sockfd;
        }
        int nfds = (max_fd + 1);
#endif

        int fds_ready = select(nfds, &read_fds, &write_fds, &except_fds, NULL);
        if (FD_ISSET(server->cmd_pipe[0], &read_fds))
            break;

        for (size_t idx=0; idx<zv_array_get_size(clients); idx++)
        {
            struct ss_client_s * client = &clients[idx];
            if (FD_ISSET(client->sockfd, &read_fds))
            {
                ssize_t bytes_received = recv(client->sockfd, buffer, (int)buffer_size, 0);
                if (bytes_received > 0)
                    server->input_handler(client, buffer, bytes_received);
                else
                {
                    server->close_handler(client);
                    zv_socket_close(client->sockfd);
                    zv_array_remove_at(clients, idx);
                    continue;
                }
            }

            if (FD_ISSET(client->sockfd, &write_fds))
            {
                client->wait_wr = 0;
                server->write_handler(client);
            }
        }

        if (FD_ISSET(server->accept_fd, &read_fds))
        {
            struct sockaddr_in peer_addr;
            socklen_t peer_addrlen = sizeof(peer_addr);
            zv_socket_t sockfd = accept(server->accept_fd, (struct sockaddr*)&peer_addr, &peer_addrlen);
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
                if (getsockname(sockfd, (struct sockaddr*)&local_addr, &local_addrlen) == 0)
                {
                    struct ss_client_s * client = zv_array_append(clients);
                    client->sockfd = sockfd;
                    zv_inet_ntop(
                        (const struct sockaddr*)&local_addr,
                        sizeof(local_addr),
                        client->local_addr_str,
                        sizeof(client->local_addr_str)
                    );
                    zv_inet_ntop(
                        (const struct sockaddr*)&peer_addr,
                        sizeof(peer_addr),
                        client->peer_addr_str,
                        sizeof(client->peer_addr_str)
                    );
                    client->wait_wr = 0;
                    client->extra_data = NULL;
                    server->accept_handler(client);
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
#else
    close(server->cmd_pipe[0]);
    close(server->cmd_pipe[1]);
#endif

    zv_socket_close(server->accept_fd);

    for (size_t idx=0; idx<zv_array_get_size(clients); idx++)
    {
        struct ss_client_s * client = &clients[idx];
        zv_socket_close(client->sockfd);
    }

    free(buffer);
    zv_array_destroy(clients);

    return NULL;
}


static int _ss_server_init(
    ss_server_t * server,
    in_port_t * port,
    void (*accept_handler)(struct ss_client_s*),
    void (*input_handler)(struct ss_client_s*, const void*, size_t),
    void (*write_handler)(struct ss_client_s*),
    void (*close_handler)(struct ss_client_s*)
) {
#if defined(_WIN32)
    SOCKET cmd_pipe[2];
    int rc = socketpair(AF_INET, SOCK_STREAM, IPPROTO_TCP, cmd_pipe);
    if (rc != 0)
    {
        fprintf(stderr, "socketpair() failed\n");
        return -1;
    }
#else
    int cmd_pipe[2];
    int rc = pipe(cmd_pipe);
    if (rc != 0)
    {
        fprintf(stderr, "pipe() failed\n");
        return -1;
    }
#endif

    zv_socket_t sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd != INVALID_SOCKET)
    {
        struct sockaddr_in addr = make_sockaddr_in(INADDR_ANY, 0);
        rc = bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
        if (rc == 0)
        {
            socklen_t namelen = sizeof(addr);
            rc = getsockname(sockfd, (struct sockaddr*)&addr, &namelen);
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
                        server->accept_handler = accept_handler;
                        server->input_handler = input_handler;
                        server->write_handler = write_handler;
                        server->close_handler = close_handler;
                        server->cmd_pipe[0] = cmd_pipe[0];
                        server->cmd_pipe[1] = cmd_pipe[1];
                        server->accept_fd = sockfd;

                        rc = pthread_create(
                            &server->thread,
                            NULL,
                            &_ss_server_thread_func,
                            server
                        );
                        if (rc == 0)
                            return 0;
                        else
                            fprintf(stderr, "pthread_create() failed\n");
                    }
                    else
                        fprintf(stderr, "listen() failed\n");
                }
                else
                    fprintf(stderr, "fcntl() failed\n");
            }
            else
                fprintf(stderr, "getsockname() failed\n");
        }
        else
            fprintf(stderr, "bind() failed\n");

        zv_socket_close(sockfd);
    }
    else
        fprintf(stderr, "socket() failed\n");

    zv_socket_close(cmd_pipe[0]);
    zv_socket_close(cmd_pipe[1]);
    return -1;
}


ss_server_t * ss_server_create(
    in_port_t * port,
    void (*accept_handler)(struct ss_client_s*),
    void (*input_handler)(struct ss_client_s*, const void*, size_t),
    void (*write_handler)(struct ss_client_s*),
    void (*close_handler)(struct ss_client_s*)
) {
    struct ss_server_s * server = malloc(sizeof(struct ss_server_s));
    if (server != NULL)
    {
        int rc = _ss_server_init(
            server,
            port,
            accept_handler,
            input_handler,
            write_handler,
            close_handler
        );
        if (rc == 0)
            return server;
        else
            free(server);
    }
    else
        fprintf(stderr, "malloc() failed\n");
    return NULL;
}


void ss_server_stop_and_destroy(ss_server_t * server)
{
    char cmd = 0;
#if defined(_WIN32)
    int rc = send(server->cmd_pipe[1], &cmd, sizeof(cmd), 0);
#else
    ssize_t rc = write(server->cmd_pipe[1], &cmd, sizeof(cmd));
#endif
    assert(rc == sizeof(cmd));
    (void)(rc);

    pthread_join(server->thread, NULL);
    free(server);
}
