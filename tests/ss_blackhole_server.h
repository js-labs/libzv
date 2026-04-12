#if !defined(SS_BLACKHOLE_SERVER_H)
#define SS_BLACKHOLE_SERVER_H

#include "zv/os_socket.h"

typedef struct ss_server_s ss_blackhole_server_t;

ss_blackhole_server_t * ss_blackhole_server_create(in_port_t * port);
void ss_blackhole_server_stop_and_destroy(ss_blackhole_server_t * server);

#endif /* !defined(SS_BLACKHOLE_SERVER_H) */
