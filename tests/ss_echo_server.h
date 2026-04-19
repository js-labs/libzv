#if !defined(SS_ECHO_SERVER_H)
#define SS_ECHO_SERVER_H

#include "zv/os_socket.h"

typedef struct ss_echo_server_s ss_echo_server_t;

ss_echo_server_t * ss_echo_server_create(in_port_t * port);
void ss_echo_server_stop_and_destroy(ss_echo_server_t * server);

#endif /* !defined(SS_ECHO_SERVER_H) */
