#include "zv/os_socket.h"
#include "zv/os_thread.h"
#include "zv/os_time.h"
#include "helpers.h"
#include "udp_echo_server.h"

#if !defined(_WIN32)
#include <fcntl.h>
#endif

#include <assert.h>

struct udp_echo_server_zv_s
{
    zv_loop_t * loop;
};

struct udp_echo_server_sock_s
{
    zv_socket_t cmd_pipe[2];
    zv_socket_t sockfd;
};

struct udp_echo_server_s
{
    int use_libxv;
    pthread_t thread;

    union
    {
        struct udp_echo_server_zv_s xv;
        struct udp_echo_server_sock_s sock;
    };
};


static int udp_echo_server_zv_init(
    struct udp_echo_server_s * server,
    struct zv_loop_config_s * loop_config,
    in_port_t * port
) {
    return -1;
}


static void * udp_echo_server_sock_thread_func(void * arg)
{
    struct udp_echo_server_sock_s * server = arg;
    zv_socket_t cmd_fd = server->cmd_pipe[0];
    fd_set read_fds;
    fd_set write_fds;
    fd_set except_fds;

    for (;;)
    {
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_ZERO(&except_fds);

        FD_SET(cmd_fd, &read_fds);
        FD_SET(server->sockfd, &read_fds);

#if defined(_WIN32)
        int nfds = 0;
#else
        int max_fd = cmd_fd;
        if (server->sockfd > max_fd)
            max_fd = server->sockfd;
        int nfds = (max_fd + 1);
#endif

        int fds_ready = select(nfds, &read_fds, &write_fds, &except_fds, NULL);
        if (FD_ISSET(cmd_fd, &read_fds))
            break;

        if (FD_ISSET(server->sockfd, &read_fds))
        {
            char buffer[1600];
            struct sockaddr_in peer_addr;
            socklen_t peer_addrlen = sizeof(peer_addr);

            ssize_t bytes_received = recvfrom(
                server->sockfd,
                buffer,
                sizeof(buffer),
                0,
                (struct sockaddr*) & peer_addr,
                &peer_addrlen
            );

            if (bytes_received > 0)
            {
                ssize_t bytes_sent = sendto(
                    server->sockfd,
                    buffer,
                    bytes_received,
                    0,
                    (struct sockaddr*) &peer_addr,
                    peer_addrlen
                );
                if (bytes_sent < 0)
                    fprintf(stderr, "sendto() failed\n");
            }
        }
    }

#if defined(_WIN32)
    closesocket(server->cmd_pipe[0]);
    closesocket(server->cmd_pipe[1]);
    closesocket(server->sockfd);
#else
    close(server->cmd_pipe[0]);
    close(server->cmd_pipe[1]);
    close(server->sockfd);
#endif

    printf("UDP echo server stopped\n");
    return NULL;
}


static int udp_echo_server_sock_init(
    struct udp_echo_server_s * server,
    in_port_t * port
) {
#if defined(_WIN32)
    SOCKET cmd_pipe[2];
    int rc = socketpair(AF_INET, SOCK_STREAM, IPPROTO_TCP, cmd_pipe);
    if (rc != 0)
    {
        fprintf(stderr, "socketpair() failed\n");
        return rc;
    }
#else
    int cmd_pipe[2];
    int rc = pipe(cmd_pipe);
    if (rc != 0)
    {
        fprintf(stderr, "pipe() failed\n");
        return rc;
    }
#endif

    zv_socket_t sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd != INVALID_SOCKET)
    {
        struct sockaddr_in addr = make_sockaddr_in(INADDR_ANY, 0);
        rc = bind(sockfd, (struct sockaddr*) &addr, sizeof(addr));
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
                    *port = ntohs(addr.sin_port);
                    server->sock.cmd_pipe[0] = cmd_pipe[0];
                    server->sock.cmd_pipe[1] = cmd_pipe[1];
                    server->sock.sockfd = sockfd;

                    printf("UDP echo server (SOCK) started on port %hu\n", *port);

                    rc = pthread_create(
                        &server->thread,
                        NULL,
                        &udp_echo_server_sock_thread_func,
                        &server->sock
                    );
                    if (rc == 0)
                        return rc;
                    else
                        fprintf(stderr, "pthread_create() failed\n");
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

#if defined(_WIN32)
    closesocket(cmd_pipe[0]);
    closesocket(cmd_pipe[1]);
#else
    close(cmd_pipe[0]);
    close(cmd_pipe[1]);
#endif
    return -1;
}


udp_echo_server_t * udp_echo_server_create(
    struct zv_loop_config_s * loop_config,
    in_port_t * port
) {
    struct udp_echo_server_s * server = malloc(sizeof(struct udp_echo_server_s));
    if (server == NULL)
    {
        fprintf(stderr, "malloc() failed\n");
        return NULL;
    }

    int rc;
    if (loop_config == NULL)
    {
        server->use_libxv = 0;
        rc = udp_echo_server_sock_init(server, port);
    }
    else
    {
        server->use_libxv = 1;
        rc = udp_echo_server_zv_init(server, loop_config, port);
    }

    if (rc == 0)
        return server;
    else
    {
        free(server);
        return NULL;
    }
}


void udp_echo_server_stop_and_destroy(udp_echo_server_t * server)
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
