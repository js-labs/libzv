#if !defined(UDP_ECHO_SERVER_H)
#define UDP_ECHO_SERVER_H

#include "zv/loop.h"
#include "zv/os_thread.h"
#include "zv/os_socket.h"

typedef struct udp_echo_server_s udp_echo_server_t;

udp_echo_server_t * udp_echo_server_create(
    struct zv_loop_config_s * loop_config,
    in_port_t * port
);

void udp_echo_server_stop_and_destroy(udp_echo_server_t * server);

#endif /* !defined(UDP_ECHO_SERVER_H) */
