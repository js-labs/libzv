#if !defined(TCP_BROADCAST_SERVER_H)
#define TCP_BROADCAST_SERVER_H

#include "zv/loop.h"
#include "zv/os_socket.h"

#define MSG_CONNECTED_CLIENTS 0x01
#define MSG_TEST_MESSAGE      0x02
#define MSG_CLIENT_COMPLETED  0x03

typedef struct tcp_broadcast_server_s tcp_broadcast_server_t;

struct msg_connected_clients_s
{
    uint16_t size;
    uint16_t id;
    uint16_t count;
};

struct msg_test_message_s
{
    uint16_t size;
    uint16_t id;
    size_t message_count;
    char extra_data;
};

struct msg_client_completed_s
{
    uint16_t size;
    uint16_t id;
    struct timespec ts;
};

tcp_broadcast_server_t * tcp_broadcast_server_create(
    struct zv_loop_config_s * loop_config,
    in_port_t * port
);

void tcp_broadcast_server_stop_and_destroy(tcp_broadcast_server_t * server);

#endif /* !defined(TCP_BROADCAST_SERVER_H) */
