/*
 * Copyright (C) 2025 Sergey Zubarev, info@js-labs.org
 *
 * This file is a part of UIO framework.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "zv/dns_resolver.h"
#include "zv/thread_pool.h"
#include "zv/semaphore.h"

#if defined(_WIN32)
#include <WinSock2.h>
#elif defined(__linux__)
#include <arpa/inet.h>
#endif

struct test_s
{
    int rc;
    sem_t sem;
};

static void after_resolve(const struct addrinfo * result, void * extra_data)
{
    struct test_s * test = extra_data;
    const struct addrinfo * addrinfo = result;
    while (addrinfo != NULL)
    {
        char buffer[INET6_ADDRSTRLEN + 6];
        const char * str = NULL;

        if (addrinfo->ai_family == AF_INET)
        {
            const struct sockaddr_in * addr = (const struct sockaddr_in*) addrinfo->ai_addr;
            str = inet_ntop(AF_INET, &addr->sin_addr, buffer, sizeof(buffer));
        }
        else if (addrinfo->ai_family == AF_INET6)
        {
            const struct sockaddr_in6 * addr = (const struct sockaddr_in6*) addrinfo->ai_addr;
            str = inet_ntop(AF_INET6, &addr->sin6_addr, buffer, sizeof(buffer));
        }
        else
        {
            printf("Unsupported address family %d\n", addrinfo->ai_family);
            addrinfo = addrinfo->ai_next;
            continue;
        }

        if (str == NULL)
            printf("inet_ntop() failed\n");
        else
            printf("%s\n", str);

        addrinfo = addrinfo->ai_next;
    }
    test->rc = 0;
    sem_post(&test->sem);
}

static void after_fail(void * thunk, int errcode)
{
    struct test_s * test = thunk;
    test->rc = -1;
    sem_post(&test->sem);
}

int dns_resolver1()
{
    int rc = 0;
    zv_thread_pool_t * thread_pool = zv_thread_pool_create(2);
    if (thread_pool != NULL)
    {
        struct test_s test;
        rc = sem_init(&test.sem, 0, 0);
        if (rc == 0)
        {
            rc = zv_dns_resolver_create(
                thread_pool,
                "www.google.com",
                "http",
                SOCK_STREAM,
                &after_resolve,
                &after_fail,
                &test
            );

            if (rc == 0)
            {
                sem_wait(&test.sem);
                rc = test.rc;
            }
            else
                fprintf(stderr, "zv_dns_resolver_create() failed\n");

            sem_destroy(&test.sem);
        }
        else
            fprintf(stderr, "sem_init() failed\n");

        zv_thread_pool_destroy(thread_pool);
    }
    else
        fprintf(stderr, "zv_thread_pool_create() failed\n");

    return 0;
}
