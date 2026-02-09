#include "zv/loop.h"
#include "zv/os_socket.h"
#include "zv/os_thread.h"
#include <string.h>

int start_server(pthread_t * thread, zv_loop_t ** loop, in_port_t * port);
int start_clients(pthread_t * thread, zv_loop_t ** loop, in_port_t server_port, int count);

static int print_usage()
{
    return -1;
}

int main(int argc, const char * argv[])
{
    in_port_t server_port = 0;
    int clients = 1;
    int rc;
    int server = 0;
    pthread_t server_thread;
    zv_loop_t * server_loop;
    int client = 0;
    pthread_t client_thread;
    zv_loop_t * client_loop;

    if (argc < 2)
        return print_usage();

    if (argc == 1)
    {
        server = 1;
        client = 1;
    }
    else
    {
        int idx = 1;
        if (strcmp(argv[idx], "server") == 0)
        {
            server = 1;
            idx++;
        }
        else if (strcmp(argv[idx], "client") == 0)
        {
            client = 1;
            idx++;
        }

        while (idx < argc)
        {
            if (strcmp(argv[idx], "-p") == 0)
            {
                if (++idx == argc)
                {
                    printf("option requires an argument -- p\n");
                    return -1;
                }
                if (sscanf(argv[idx], "%hu", &server_port) != 1)
                {
                    printf("invalid server port `%s'\n", argv[idx]);
                    return -1;
                }
                idx++;
            }
            else if (strcmp(argv[idx], "-c") == 0)
            {
                if (++idx == argc)
                {
                    printf("option requires an argument -- c\n");
                    return -1;
                }
                if (sscanf(argv[idx], "%d", &clients) != 1)
                {
                    printf("invalid numver of clients `%s'\n", argv[idx]);
                    return -1;
                }
            }
            else
                return print_usage();
        }
    }

    if (server)
    {
        rc = start_server(&server_thread, &server_loop, &server_port);
        if (rc != 0)
            return rc;
    }

    if (client)
    {
        rc = start_clients(&client_thread, &client_loop, server_port, clients);
        if (rc != 0)
            return rc;
    }

    if (server)
        pthread_join(server_thread, NULL);

    if (client)
        pthread_join(client_thread, NULL);

    return 0;
}
