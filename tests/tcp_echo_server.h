#if !defined(TCP_ECHO_SERVER_H)
#define TCP_ECHO_SERVER_H

#include "zv/loop.h"
#include "zv/os_thread.h"
#include "zv/os_socket.h"

typedef struct tcp_echo_server_s tcp_echo_server_t;

tcp_echo_server_t * tcp_echo_server_create(
    struct zv_loop_config_s * loop_config,
    in_port_t * port
);

void tcp_echo_server_stop_and_destroy(tcp_echo_server_t * server);

#endif /* !defined(TCP_ECHO_SERVER_H) */
