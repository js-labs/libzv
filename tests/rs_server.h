#if !defined(RS_SERVER_H)
#define RS_SERVER_H

#include "zv/os_socket.h"

// simple raw socket server

typedef struct rs_server_s rs_server_t;

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

rs_server_t * rs_server_create(
    in_port_t * port,
    void (*accept_handler)(struct ss_client_s*),
    void (*input_handler)(struct ss_client_s*, const void *, size_t),
    void (*write_handler)(struct ss_client_s*),
    void (*close_handler)(struct ss_client_s*)
);

void rs_server_stop_and_destroy(rs_server_t * server);

#endif /* !defined(RS_SERVER_H) */
