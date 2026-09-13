#include "rs_blackhole_server.h"
#include "rs_server.h"
#include <stdio.h>
#include <stdint.h>


static void _rs_blackhole_server_accept_handler(struct ss_client_s * client)
{
    // do nothing
}


static void _rs_blackhole_server_input_handler(
    struct ss_client_s * client,
    const void * data,
    size_t size
) {
    uintptr_t bytes_received = (uintptr_t) client->extra_data;
    bytes_received += size;
    client->extra_data = (void*) bytes_received;
}


static void _rs_blackhole_server_write_handler(struct ss_client_s * client)
{
    // do nothing
}


static void _rs_blackhole_server_close_handler(struct ss_client_s * client)
{
    /*
    uintptr_t bytes_received = client->extra_data;
    printf("received %zu bytes\n", bytes_received);
    */
}


rs_blackhole_server_t * rs_blackhole_server_create(in_port_t * port)
{
    rs_server_t * server = rs_server_create(
        port,
        &_rs_blackhole_server_accept_handler,
        &_rs_blackhole_server_input_handler,
        &_rs_blackhole_server_write_handler,
        &_rs_blackhole_server_close_handler
    );
    if (server != NULL)
        printf("TCP blackhole server started on port %hu\n", *port);
    return (rs_blackhole_server_t*) server;
}


void rs_blackhole_server_stop_and_destroy(rs_blackhole_server_t * server)
{
    rs_server_stop_and_destroy((rs_server_t*)server);
}
