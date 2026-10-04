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

#if !defined (ZV_DNS_RESOLVER_H)
#define ZV_DNS_RESOLVER_H

#include "zv/export.h"

#if defined(_WIN32)
#include <ws2tcpip.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

typedef struct zv_thread_pool_s zv_thread_pool_t;
typedef struct zv_dns_resolver_s zv_dns_resolver_t;

ZV_EXPORT int zv_dns_resolver_create(
    zv_thread_pool_t * thread_pool,
    const char * host,
    const char * service,
    int sock_type,
    void (*after_resolve)(const struct addrinfo *, void *),
    void (*after_fail)(void*, /*errcode*/int),
    void * extra_data
);

ZV_EXPORT extern void * _zv_dns_resolver_addrs;

#endif /* !defined(ZV_DNS_RESOLVER_H) */
