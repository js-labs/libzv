#include <stdio.h>
#include <stdlib.h>
#include "zv/acceptor.h"
#include "zv/connector.h"
#include "zv/he_connector.h"
#include "zv/channel.h"
#include "zv/log.h"
#include "zv/loop.h"
#include "helpers.h"

int acceptor1();
int buffer_cache();
int channel_close1();
int channel_close2();
int channel_recv();
int channel_recv_suspend();
int channel_send_peer_close();
int connector1();
int connector2();
int connector3();
int dns_resolver1();
int he_connector1();
int ipv6();
int stream_defragger1();
int timer_mgr_test1();
int timer_mgr_test2();
int timer_mgr_test3();
int timer_mgr_test4();
int timer_mgr_test5();

void run_test(int * failed_tests, const char * name, int (*test_func)())
{
    printf("### RUN [%s]:\n", name);

    zv_log_backend_t * log_backend = NULL;
    const char * fmt = "%s.log";
    int log_file_name_len = snprintf(NULL, 0, fmt, name);
    char * log_file_name = malloc(log_file_name_len + 1);
    if (log_file_name)
    {
        snprintf(log_file_name, log_file_name_len+1, fmt, name);
        FILE * file = fopen(log_file_name, "w");
        log_backend = zv_log_add_file_backend(file);
        free(log_file_name);
        log_file_name = NULL;
    }

    int rc = (*test_func)();
    if (rc)
    {
        printf("=== FAILED\n");
        (*failed_tests)++;
    }
    else
        printf("=== passed\n");

    fflush(stdout);

    if (log_backend)
    {
        FILE * file = zv_log_remove_file_backend(log_backend);
        if (file)
            fclose(file);
    }
}

int main(int argc, const char * argv[])
{
#if defined(_WIN32)
    if (wsa_init() != 0)
    {
        fprintf(stderr, "wsa_init() failed\n");
        return -1;
    }
#endif

    int failed_tests = 0;
    int tests = 0;

    zv_log_add_file_backend(stdout);
    zv_acceptor_set_log_level(zv_log_level_debug);
    zv_connector_set_log_level(zv_log_level_debug);
    zv_channel_set_log_level(zv_log_level_debug);
    zv_he_connector_set_log_level(zv_log_level_debug);
    zv_loop_set_log_level(zv_log_level_debug);

#define RUN_TEST(name) \
    run_test(&failed_tests, #name, name); tests++

    /*
    RUN_TEST(acceptor1);
    RUN_TEST(buffer_cache);
    RUN_TEST(channel_close1);
    RUN_TEST(channel_close2);
    RUN_TEST(channel_recv);
    RUN_TEST(channel_recv_suspend);
    RUN_TEST(channel_send_peer_close);
    RUN_TEST(connector1);
    RUN_TEST(connector2);
    */
    //RUN_TEST(connector3);
    //RUN_TEST(dns_resolver1);
    RUN_TEST(he_connector1);
    /*
    RUN_TEST(ipv6);
    RUN_TEST(stream_defragger1);
    RUN_TEST(timer_mgr_test1);
    RUN_TEST(timer_mgr_test2);
    RUN_TEST(timer_mgr_test3);
    RUN_TEST(timer_mgr_test4);
    RUN_TEST(timer_mgr_test5);
    */

#undef RUN_TEST

#if defined(_WIN32)
    WSACleanup();
#endif

    if (failed_tests > 0)
    {
        printf("%d out of %d tests failed\n", failed_tests, tests);
        return -1;
    }
    else
        return 0;
}
