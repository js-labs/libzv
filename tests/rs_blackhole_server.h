#if !defined(RS_BLACKHOLE_SERVER_H)
#define RS_BLACKHOLE_SERVER_H

#include "zv/os_socket.h"

typedef struct rs_blackhole_server_s rs_blackhole_server_t;

rs_blackhole_server_t * rs_blackhole_server_create(in_port_t * port);
void rs_blackhole_server_stop_and_destroy(rs_blackhole_server_t * server);

#endif /* !defined(RS_BLACKHOLE_SERVER_H) */
