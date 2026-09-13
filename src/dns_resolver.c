/*
 * Copyright (C) 2022 Sergey Zubarev, info@js-labs.org
 *
 * This file is a part of ZV framework.
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
#include "zv/err.h"
#include "zv/os_thread.h"
#include "zv/thread_pool.h"
#include "misc.h"

#if !defined(_WIN32)
#include <string.h>
#endif

// to be used for tests
void * _zv_dns_resolver_addrs = NULL;


struct zv_dns_resolver_s
{
    struct zv_thread_pool_task_s task;
    const char * host;
    const char * service;
    int sock_type;
    void (*after_resolve)(const struct addrinfo*, void*);
    void (*after_fail)(void*, int);
    void * extra_data;
};


static void _zv_dns_resolver_task(struct zv_thread_pool_task_s * task)
{
    struct zv_dns_resolver_s * resolver;
    resolver = _ZV_CONTAINER_OF(task, struct zv_dns_resolver_s, task);

    if (_zv_dns_resolver_addrs == NULL)
    {
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = resolver->sock_type;

        struct addrinfo * result;
        int rc = getaddrinfo(resolver->host, resolver->service, &hints, &result);
        if (rc == 0)
        {
            resolver->after_resolve(result, resolver->extra_data);
            freeaddrinfo(result);
        }
        else
            resolver->after_fail(resolver->extra_data, rc);
    }
    else
    {
        if (_zv_dns_resolver_addrs == ((void*)1))
            resolver->after_fail(resolver->extra_data, 0);
        else
        {
            resolver->after_resolve(_zv_dns_resolver_addrs, resolver->extra_data);
            free(_zv_dns_resolver_addrs);
        }
        _zv_dns_resolver_addrs = NULL;
    }

    free(resolver);
}


int zv_dns_resolver_create(
    zv_thread_pool_t * thread_pool,
    const char * host,
    const char * service,
    int sock_type,
    void (*after_resolve)(const struct addrinfo*, void*),
    void (*after_fail)(void*, int),
    void * extra_data
) {
    if ((host == NULL) && (service == NULL))
        return ZV_ERR_INVAL;

    size_t host_size = (host == NULL) ? 0 : (strlen(host) + 1);
    size_t service_size = (service == NULL) ? 0 : (strlen(service) + 1);
    size_t size = sizeof(struct zv_dns_resolver_s) + host_size + service_size;

    struct zv_dns_resolver_s * resolver = malloc(size);
    if (resolver == NULL)
        return ZV_ERR_NOMEM;

    zv_thread_pool_task_init(&resolver->task, &_zv_dns_resolver_task);

    char * ptr = (((char*)resolver) + sizeof(struct zv_dns_resolver_s));
    if (host_size > 0)
    {
        resolver->host = ptr;
        memcpy(ptr, host, host_size);
        ptr += host_size;
    }
    else
        resolver->host = NULL;

    if (service_size > 0)
    {
        resolver->service = ptr;
        memcpy(ptr, service, service_size);
        ptr += service_size;
    }
    else
        resolver->service = NULL;

    resolver->sock_type = sock_type;
    resolver->after_resolve = after_resolve;
    resolver->after_fail = after_fail;
    resolver->extra_data = extra_data;

    zv_thread_pool_dispatch(thread_pool, &resolver->task);
    return 0;
}
