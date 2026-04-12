#if !defined(SS_SERVER_H)
#define SS_SERVER_H

#include "zv/os_socket.h"

// simple socket server

typedef struct ss_server_s ss_server_t;

#if !defined(INET_ADDRSTRLEN)
#define INET_ADDRSTRLEN 22
#endif

struct ss_client_s
{
    zv_socket_t sockfd;
    char local_addr_str[INET_ADDRSTRLEN];
    char peer_addr_str[INET_ADDRSTRLEN];
    char wait_wr;
    void * extra_data;
};

ss_server_t * ss_server_create(
    in_port_t * port,
    void (*accept_handler)(struct ss_client_s*),
    void (*input_handler)(struct ss_client_s*, const void *, size_t),
    void (*write_handler)(struct ss_client_s*),
    void (*close_handler)(struct ss_client_s*)
);

void ss_server_stop_and_destroy(ss_server_t * server);

#endif /* !defined(SS_SERVER_H) */
