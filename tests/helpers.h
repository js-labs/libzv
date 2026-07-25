#if !defined(ZV_TESTS_HELPERS_H)
#define ZV_TESTS_HELPERS_H

#include "zv/acceptor.h"
#include "zv/config.h"
#include "zv/connector.h"
#include "zv/loop.h"
#include "zv/os_socket.h"
#include "zv/os_thread.h"
#include "zv/timer_pool.h"
#include <stdint.h>

struct sockaddr_in make_sockaddr_in(in_addr_t in_addr, in_port_t in_port);

#if defined(_WIN32)
int wsa_init();
#endif

int get_sock_names(
    zv_socket_t sockfd,
    char * local_addr_str, int local_addr_str_len,
    char * peer_addr_str, int peer_addr_str_len
);

zv_loop_t * start_loop(
    pthread_t * loop_thread,
    const struct zv_loop_config_s * loop_config
);

zv_acceptor_t * start_acceptor_with_buffer_cache(
    zv_loop_t * loop,
    void (*accept_handler)(zv_acceptor_t*, zv_socket_t)
);

zv_loop_t * create_loop_and_connector(
    in_port_t port,
    void (*after_connect)(zv_connector_t*, zv_socket_t)
);

int accept_async(
    in_port_t * port,
    pthread_t * thread,
    void (*after_accept)(zv_socket_t, void*),
    void * thunk
);

zv_socket_t sock_connect(in_addr_t in_addr, in_port_t in_port);

int run_timer_pool_test(int (*test_func)(zv_timer_pool_t*));

long timeval_elapsed_time_us(const struct timeval * start, const struct timeval * end);
void timeval_add_ms(struct timeval * tv, long ms);

#endif /* !defined(ZV_TESTS_HELPERS_H) */
