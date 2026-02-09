#include "zv/buffer.h"
#include "zv/loop.h"
#include "zv/os_socket.h"
#include "zv/os_thread.h"
#include "helpers.h"

struct client_s
{
    zv_buffer_cache_t * buffer_cache;
    zv_loop_t * loop;
};

int start_clients(pthread_t * thread, zv_loop_t ** loop, in_port_t server_port, int count)
{
    return -1;
}
