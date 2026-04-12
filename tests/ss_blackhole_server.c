#include "ss_blackhole_server.h"
#include "ss_server.h"
#include <stdio.h>
#include <stdint.h>


static void _ss_blackhole_server_accept_handler(struct ss_client_s * client)
{
    // do nothing
}


static void _ss_blackhole_server_input_handler(
    struct ss_client_s * client,
    const void * data,
    size_t size
) {
    uintptr_t bytes_received = client->extra_data;
    bytes_received += size;
    client->extra_data = bytes_received;
}


void _ss_blackhole_server_write_handler(struct ss_client_s * client)
{
    // do nothing
}


void _ss_blackhole_server_close_handler(struct ss_client_s * client)
{
    /*
    uintptr_t bytes_received = client->extra_data;
    printf("received %zu bytes\n", bytes_received);
    */
}


ss_blackhole_server_t * ss_blackhole_server_create(in_port_t * port)
{
    ss_server_t * server = ss_server_create(
        port,
        &_ss_blackhole_server_accept_handler,
        &_ss_blackhole_server_input_handler,
        &_ss_blackhole_server_write_handler,
        &_ss_blackhole_server_close_handler
    );
    if (server != NULL)
        printf("Blackhole server (SS) started on port %hu\n", *port);
    return server;
}


void ss_blackhole_server_stop_and_destroy(ss_blackhole_server_t * server)
{
    ss_server_stop_and_destroy(server);
}
