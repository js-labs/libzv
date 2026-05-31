#if !defined(RS_ECHO_SERVER_H)
#define RS_ECHO_SERVER_H

#include "zv/os_socket.h"

// Raw socket echo server

typedef struct rs_echo_server_s rs_echo_server_t;

rs_echo_server_t * rs_echo_server_create(in_port_t * port);
void rs_echo_server_stop_and_destroy(rs_echo_server_t * server);

#endif /* !defined(RS_ECHO_SERVER_H) */
