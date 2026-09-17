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

#include "zv/connector.h"
#include "zv/dns_resolver.h"
#include "zv/err.h"
#include "zv/he_connector.h"
#include "zv/log.h"
#include "zv/loop.h"
#include "zv/os_socket.h"
#include "zv/os_thread.h"
#include "zv/os_time.h"
#include "zv/thread_pool.h"
#include "zv/timer_mgr.h"
#include "misc.h"
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>

#if !defined(_WIN32)
#include <string.h>
#endif

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

#define CONNECT_INTERVAL_MS 250
#define MAX_PENDING_CONNECTORS 4

struct zv_log_layer_s _zv_he_connector_log = { "zv.he_connector", zv_log_level_warning };

void zv_he_connector_set_log_level(enum zv_log_level level)
{
    _zv_he_connector_log.level = level;
}

enum zv_he_connector_state_e
{
    zv_he_connector_state_resolving,
    zv_he_connector_state_connecting,
    zv_he_connector_state_cb,
    zv_he_connector_state_done
};

struct connect_info_s
{
    struct zv_he_connector_s * he_connector;
    int sockaddr_type;
    union
    {
        struct sockaddr_in v4;
        struct sockaddr_in6 v6;
    }
    sockaddr;
};

struct zv_he_connector_after_stop_s
{
    struct zv_he_connector_after_stop_s * next;
    void (*func)(zv_he_connector_t*, void*);
    void * arg;
};

struct zv_he_connector_after_stop_task_s
{
    struct zv_he_connector_after_stop_s after_stop;
    struct zv_thread_pool_task_s task;
    struct zv_he_connector_s * he_connector;
};

struct zv_he_connector_s
{
    _Atomic size_t retain_count;

    zv_loop_t * loop;
    const char * host;
    const char * service;
    void (*after_connect)(zv_he_connector_t*, zv_socket_t);
    void (*after_fail)(zv_he_connector_t*, int);
    void * extra_data;

    pthread_mutex_t mutex;
    enum zv_he_connector_state_e state;
    char stop;
    char timer;
    unsigned char pending_connectors;
    size_t count;
    size_t next_idx;
    struct connect_info_s * connect_info;
    struct zv_he_connector_after_stop_s * after_stop;
};


static void _zv_he_connector_call_after_stop(
    struct zv_he_connector_s * he_connector,
    struct zv_he_connector_after_stop_s * after_stop
) {
    while (after_stop != NULL)
    {
        struct zv_he_connector_after_stop_s * next = after_stop->next;
        void (*func)(zv_he_connector_t*, void*) = after_stop->func;
        void * arg = after_stop->arg;
        free(after_stop);
        func(he_connector, arg);
        after_stop = next;
    }
}


static struct zv_he_connector_after_stop_s * _zv_he_connector_set_done_locked(
    struct zv_he_connector_s * he_connector
) {
    he_connector->state = zv_he_connector_state_done;
    struct zv_he_connector_after_stop_s * after_stop = he_connector->after_stop;
    he_connector->after_stop = NULL;
    return after_stop;
}


static void _zv_he_connector_after_connect(
    zv_connector_t * connector,
    zv_socket_t sockfd
) {
    struct connect_info_s * connect_info = zv_connector_get_extra_data(connector);
    struct zv_he_connector_s * he_connector = connect_info->he_connector;

    pthread_mutex_lock(&he_connector->mutex);

    zv_timer_mgr_t * timer_mgr;
    if (he_connector->timer)
    {
        timer_mgr = zv_loop_get_timer_mgr(he_connector->loop);
        he_connector->timer = 0;
    }
    else
        timer_mgr = NULL;

    unsigned char pending_connectors;
    struct zv_he_connector_after_stop_s * after_stop = NULL;

    if (he_connector->stop)
    {
        pending_connectors = --he_connector->pending_connectors;
        if (he_connector->state == zv_he_connector_state_connecting)
            after_stop = _zv_he_connector_set_done_locked(he_connector);
        pthread_mutex_unlock(&he_connector->mutex);
        zv_socket_close(sockfd);
    }
    else if (he_connector->state == zv_he_connector_state_connecting)
    {
        he_connector->state = zv_he_connector_state_cb;
        pthread_mutex_unlock(&he_connector->mutex);

        // cancel the timer before callback to reduce number of references
        if (timer_mgr != NULL)
        {
            int rc = zv_timer_mgr_cancel_sync(timer_mgr, he_connector);
            if (rc == 0)
                zv_he_connector_release(he_connector);
            timer_mgr = NULL;
        }

        he_connector->after_connect(he_connector, sockfd);

        pthread_mutex_lock(&he_connector->mutex);
        pending_connectors = --he_connector->pending_connectors;
        after_stop = _zv_he_connector_set_done_locked(he_connector);
        pthread_mutex_unlock(&he_connector->mutex);
    }
    else
    {
        // state == zv_he_connector_state_cb
        // || state == zv_he_connector_state_done
        pending_connectors = --he_connector->pending_connectors;
        pthread_mutex_unlock(&he_connector->mutex);
        zv_socket_close(sockfd);
    }

    if (timer_mgr != NULL)
    {
        int rc = zv_timer_mgr_cancel_sync(timer_mgr, he_connector);
        if (rc == 0)
            zv_he_connector_release(he_connector);
    }

    _zv_he_connector_call_after_stop(he_connector, after_stop);

    if (pending_connectors == 0)
        zv_he_connector_release(he_connector);
}


static void _zv_he_connector_after_connect_fail(
    zv_connector_t * connector,
    int sockerr
);


static int _zv_he_connector_connect_next_locked(
    struct zv_he_connector_s * he_connector
) {
    struct connect_info_s * connect_info = he_connector->connect_info;
    connect_info += he_connector->next_idx;
    he_connector->next_idx++;

    struct sockaddr * sockaddr;
    socklen_t addrlen;

    if (connect_info->sockaddr_type == AF_INET)
    {
        addrlen = sizeof(struct sockaddr_in);
        sockaddr = (struct sockaddr*) &connect_info->sockaddr.v4;
    }
    else
    {
        assert(connect_info->sockaddr_type == AF_INET6);
        addrlen = sizeof(struct sockaddr_in6);
        sockaddr = (struct sockaddr*) &connect_info->sockaddr.v6;
    }

    int rc = zv_connector_create(
        he_connector->loop,
        sockaddr,
        addrlen,
        &_zv_he_connector_after_connect,
        &_zv_he_connector_after_connect_fail,
        connect_info
    );

    if (rc == 0)
        return 0;
    else
    {
        char buffer[INET6_ADDRSTRLEN + 6];
        ZV_LOG_DEBUG(
            _zv_he_connector_log,
            "0x%" PRIxPTR ": failed to connect to %s",
            he_connector,
            zv_sockaddr_ntop(sockaddr, addrlen, buffer, sizeof(buffer))
        );
        return rc;
    }
}


static void _zv_he_connector_fail_locked(
    zv_he_connector_t * he_connector,
    int errcode
) {
    he_connector->state = zv_he_connector_state_cb;

    zv_timer_mgr_t * timer_mgr;
    if (he_connector->timer)
    {
        timer_mgr = zv_loop_get_timer_mgr(he_connector->loop);
        he_connector->timer = 0;
    }
    else
        timer_mgr = NULL;

    pthread_mutex_unlock(&he_connector->mutex);

    // cancel timer first to be sure there is nothing retaining
    // the connector on the callback
    if (timer_mgr != NULL)
    {
        int rc = zv_timer_mgr_cancel_sync(timer_mgr, he_connector);
        if (rc == 0)
            zv_he_connector_release(he_connector);
    }

    he_connector->after_fail(he_connector, errcode);

    pthread_mutex_lock(&he_connector->mutex);
}


static void _zv_he_connector_after_connect_fail(
    zv_connector_t * connector,
    int sockerr
) {
    struct connect_info_s * connect_info = zv_connector_get_extra_data(connector);
    struct zv_he_connector_s * he_connector = connect_info->he_connector;

    char buffer[INET6_ADDRSTRLEN + 6];
    socklen_t addrlen;
    const struct sockaddr * addr = zv_connector_get_addr(connector, &addrlen);
    ZV_LOG_DEBUG(
        _zv_he_connector_log,
        "0x%" PRIxPTR ": failed to connect to %s",
        he_connector,
        zv_sockaddr_ntop(addr, addrlen, buffer, sizeof(buffer))
    );

    pthread_mutex_lock(&he_connector->mutex);
    if (he_connector->stop)
    {
        struct zv_he_connector_after_stop_s * after_stop;
        if (he_connector->state == zv_he_connector_state_connecting)
            after_stop = _zv_he_connector_set_done_locked(he_connector);
        else
            after_stop = NULL;

        zv_timer_mgr_t * timer_mgr;
        if (he_connector->timer)
        {
            timer_mgr = zv_loop_get_timer_mgr(he_connector->loop);
            he_connector->timer = 0;
        }
        else
            timer_mgr = NULL;

        size_t pending_connectors = --he_connector->pending_connectors;

        pthread_mutex_unlock(&he_connector->mutex);

        if (timer_mgr != NULL)
        {
            int rc = zv_timer_mgr_cancel_sync(timer_mgr, he_connector);
            if (rc == 0)
                zv_he_connector_release(he_connector);
        }

        _zv_he_connector_call_after_stop(he_connector, after_stop);

        if (pending_connectors == 0)
            zv_he_connector_release(he_connector);
    }
    else
    {
        if (he_connector->state == zv_he_connector_state_connecting)
        {
            while (he_connector->next_idx < he_connector->count)
            {
                int rc = _zv_he_connector_connect_next_locked(he_connector);
                if (rc == 0)
                {
                    pthread_mutex_unlock(&he_connector->mutex);
                    return;
                }
            }
        }

        size_t pending_connectors = --he_connector->pending_connectors;
        if (pending_connectors == 0)
        {
            struct zv_he_connector_after_stop_s * after_stop;
            if (he_connector->state == zv_he_connector_state_connecting)
            {
                _zv_he_connector_fail_locked(he_connector, sockerr);
                after_stop = _zv_he_connector_set_done_locked(he_connector);
            }
            else
                after_stop = NULL;
            pthread_mutex_unlock(&he_connector->mutex);
            _zv_he_connector_call_after_stop(he_connector, after_stop);
            zv_he_connector_release(he_connector);
        }
        else
            pthread_mutex_unlock(&he_connector->mutex);
    }
}


static struct timespec * _zv_he_connector_timer(
    void * timer,
    struct timespec * ts
) {
    struct zv_he_connector_s * he_connector = timer;
    struct zv_he_connector_after_stop_s * after_stop = NULL;

    pthread_mutex_lock(&he_connector->mutex);

    if (he_connector->stop)
    {
        ZV_LOG_ERROR(
            _zv_he_connector_log,
            "0x%" PRIxPTR ": timer, stop",
            (intptr_t) he_connector
        );

        if (he_connector->state == zv_he_connector_state_connecting)
            after_stop = _zv_he_connector_set_done_locked(he_connector);
    }
    else if (he_connector->state == zv_he_connector_state_connecting)
    {
        assert(he_connector->pending_connectors < MAX_PENDING_CONNECTORS);
        while (he_connector->next_idx < he_connector->count)
        {
            int rc = _zv_he_connector_connect_next_locked(he_connector);
            if (rc == 0)
            {
                size_t pending_connectors = ++he_connector->pending_connectors;
                if ((pending_connectors < MAX_PENDING_CONNECTORS) &&
                    (he_connector->next_idx < he_connector->count))
                {
                    pthread_mutex_unlock(&he_connector->mutex);

                    zv_timer_mgr_t * timer_mgr = zv_loop_get_timer_mgr(he_connector->loop);
                    clockid_t clock_id = zv_timer_mgr_get_clock(timer_mgr);
                    clock_gettime(clock_id, ts);
                    zv_timespec_add_ms(ts, CONNECT_INTERVAL_MS);
                    return ts;
                }
                else
                    break; // timer is not needed any more
            }
            // try next address
        }
    }

    he_connector->timer = 0;
    pthread_mutex_unlock(&he_connector->mutex);

    _zv_he_connector_call_after_stop(he_connector, after_stop);

    zv_he_connector_release(he_connector);
    return NULL;
}


static void _zv_he_connector_after_resolve(
    const struct addrinfo * result,
    void * extra_data
) {
    struct zv_he_connector_s * he_connector = extra_data;
    const struct addrinfo * addrinfo = result;
    size_t v4_count = 0;
    size_t v6_count = 0;

    while (addrinfo != NULL)
    {
        if (addrinfo->ai_family == AF_INET)
            v4_count++;
        else if (addrinfo->ai_family == AF_INET6)
            v6_count++;
        addrinfo = addrinfo->ai_next;
    }

    const size_t total_count = (v6_count + v4_count);
    ZV_LOG_DEBUG(
        _zv_he_connector_log,
        "0x%" PRIxPTR ": resolved %zu addresses",
        (intptr_t)he_connector,
        total_count
    );

    if (total_count == 0)
    {
        pthread_mutex_lock(&he_connector->mutex);
        if (!he_connector->stop)
            _zv_he_connector_fail_locked(he_connector, 0);
        struct zv_he_connector_after_stop_s * after_stop;
        after_stop = _zv_he_connector_set_done_locked(he_connector);
        pthread_mutex_unlock(&he_connector->mutex);
        _zv_he_connector_call_after_stop(he_connector, after_stop);
        zv_he_connector_release(he_connector);
        return;
    }

    size_t buffer_size = sizeof(struct connect_info_s) * total_count;
    void * buffer = malloc(buffer_size);
    if (buffer == NULL)
    {
        pthread_mutex_lock(&he_connector->mutex);
        if (!he_connector->stop)
            _zv_he_connector_fail_locked(he_connector, 0);
        struct zv_he_connector_after_stop_s * after_stop;
        after_stop = _zv_he_connector_set_done_locked(he_connector);
        pthread_mutex_unlock(&he_connector->mutex);
        _zv_he_connector_call_after_stop(he_connector, after_stop);
        zv_he_connector_release(he_connector);
        return;
    }

    struct connect_info_s * connect_info = buffer;
    const struct addrinfo * v4_ptr = result;
    const struct addrinfo * v6_ptr = result;

    while ((v4_ptr != NULL) || (v6_ptr != NULL))
    {
        while (v6_ptr != NULL)
        {
            if (v6_ptr->ai_family == AF_INET6)
            {
                assert(v6_ptr->ai_addrlen == sizeof(struct sockaddr_in6));
                connect_info->he_connector = he_connector;
                connect_info->sockaddr_type = AF_INET6;
                memcpy(&connect_info->sockaddr, v6_ptr->ai_addr, sizeof(struct sockaddr_in6));
                v6_ptr = v6_ptr->ai_next;
                connect_info++;
                break;
            }
            v6_ptr = v6_ptr->ai_next;
        }

        while (v4_ptr != NULL)
        {
            if (v4_ptr->ai_family == AF_INET)
            {
                assert(v4_ptr->ai_addrlen == sizeof(struct sockaddr_in));
                connect_info->he_connector = he_connector;
                connect_info->sockaddr_type = AF_INET;
                memcpy(&connect_info->sockaddr, v4_ptr->ai_addr, sizeof(struct sockaddr_in));
                v4_ptr = v4_ptr->ai_next;
                connect_info++;
                break;
            }
            v4_ptr = v4_ptr->ai_next;
        }
    }

    pthread_mutex_lock(&he_connector->mutex);
    if (he_connector->stop)
    {
        struct zv_he_connector_after_stop_s * after_stop;
        after_stop = _zv_he_connector_set_done_locked(he_connector);
        pthread_mutex_unlock(&he_connector->mutex);
        _zv_he_connector_call_after_stop(he_connector, after_stop);
        zv_he_connector_release(he_connector);
        free(buffer);
        return;
    }

    he_connector->state = zv_he_connector_state_connecting;
    he_connector->count = total_count;
    he_connector->next_idx = 0;
    he_connector->connect_info = buffer;

    int rc = _zv_he_connector_connect_next_locked(he_connector);
    if (rc < 0)
    {
        // zv_connector_create() failed,
        // most probably does not make sense to try any other address
        if (!he_connector->stop)
            _zv_he_connector_fail_locked(he_connector, 0);
        struct zv_he_connector_after_stop_s * after_stop;
        after_stop = _zv_he_connector_set_done_locked(he_connector);
        pthread_mutex_unlock(&he_connector->mutex);
        _zv_he_connector_call_after_stop(he_connector, after_stop);
        zv_he_connector_release(he_connector);
        return;
    }

    he_connector->pending_connectors = 1;

    if (total_count > 1)
    {
        zv_he_connector_retain(he_connector);

        zv_timer_mgr_t * timer_mgr = zv_loop_get_timer_mgr(he_connector->loop);
        clockid_t clock_id = zv_timer_mgr_get_clock(timer_mgr);

        struct timespec ts;
        clock_gettime(clock_id, &ts);
        zv_timespec_add_ms(&ts, CONNECT_INTERVAL_MS);

        int rc = zv_timer_mgr_at(
            timer_mgr,
            &ts,
            he_connector,
            &_zv_he_connector_timer
        );

        if (rc == 0)
            he_connector->timer = 1;
        else
        {
            // can fail if out of memory,
            // not sure the program will survive but still
            ZV_LOG_ERROR(
                _zv_he_connector_log,
                "0x%" PRIxPTR ": zv_timer_mgr_at() failed",
                (intptr_t)he_connector
            );
            zv_he_connector_release(he_connector);
        }
    }

    pthread_mutex_unlock(&he_connector->mutex);
}


static void _zv_he_connector_after_resolve_fail(void * extra_data, int errcode)
{
    struct zv_he_connector_s * he_connector = extra_data;
    pthread_mutex_lock(&he_connector->mutex);
    if (!he_connector->stop)
        _zv_he_connector_fail_locked(he_connector, errcode);
    struct zv_he_connector_after_stop_s * after_stop;
    after_stop = _zv_he_connector_set_done_locked(he_connector);
    pthread_mutex_unlock(&he_connector->mutex);
    _zv_he_connector_call_after_stop(he_connector, after_stop);
    zv_he_connector_release(he_connector);
}


zv_he_connector_t * zv_he_connector_create(
    zv_loop_t * loop,
    const char * host,
    const char * service,
    int sock_type,
    void (*after_connect)(zv_he_connector_t*, zv_socket_t),
    void (*after_fail)(zv_he_connector_t*, int),
    void * extra_data
) {
    size_t host_size = (host == NULL) ? 0 : (strlen(host) + 1);
    size_t service_size = (service == NULL) ? 0 : (strlen(service) + 1);
    size_t size = sizeof(struct zv_he_connector_s) + host_size + service_size;

    struct zv_he_connector_s * he_connector = malloc(size);
    if (he_connector == NULL)
        return NULL;

    // one ref for the caller and one for the workers
    he_connector->retain_count = 2;
    he_connector->loop = loop;

    if (host == NULL)
        he_connector->host = NULL;
    else
    {
        char * ptr = (char*) (he_connector + 1);
        memcpy(ptr, host, host_size);
        he_connector->host = ptr;
    }

    if (service == NULL)
        he_connector->service = NULL;
    else
    {
        char * ptr = (char*) (he_connector + 1);
        ptr += host_size;
        memcpy(ptr, service, service_size);
        he_connector->service = ptr;
    }

    he_connector->after_connect = after_connect;
    he_connector->after_fail = after_fail;
    he_connector->extra_data = extra_data;

    int last_error = 0;
    int rc = pthread_mutex_init(&he_connector->mutex, NULL);
    if (rc == 0)
    {
        he_connector->state = zv_he_connector_state_resolving;
        he_connector->stop = 0;
        he_connector->timer = 0;
        he_connector->pending_connectors = 0;
        he_connector->count = 0;
        he_connector->next_idx = 0;
        he_connector->connect_info = NULL;
        he_connector->after_stop = NULL;

        ZV_LOG_DEBUG(
            _zv_he_connector_log,
            "0x%" PRIxPTR ": host='%s' service='%s'",
            (intptr_t)he_connector,
            host,
            service
        );

        rc = zv_dns_resolver_create(
            zv_loop_get_thread_pool(loop),
            host,
            service,
            sock_type,
            &_zv_he_connector_after_resolve,
            &_zv_he_connector_after_resolve_fail,
            he_connector
        );

        if (rc == 0)
            return he_connector;

        last_error = errno;
        pthread_mutex_destroy(&he_connector->mutex);
    }

    free(he_connector);
    errno = last_error;
    return NULL;
}


zv_loop_t * zv_he_connector_get_loop(zv_he_connector_t * he_connector)
{
    return he_connector->loop;
}


const char * zv_he_connector_get_host(zv_he_connector_t * he_connector)
{
    return he_connector->host;
}


const char * zv_he_connector_get_service(zv_he_connector_t * he_connector)
{
    return he_connector->service;
}


void * zv_he_connector_get_extra_data(zv_he_connector_t * he_connector)
{
    return he_connector->extra_data;
}


static void _zv_he_connector_after_stop_task(struct zv_thread_pool_task_s * task)
{
    struct zv_he_connector_after_stop_task_s * after_stop_task;
    after_stop_task = _ZV_CONTAINER_OF(
        task,
        struct zv_he_connector_after_stop_task_s,
        task
    );
    struct zv_he_connector_s * he_connector = after_stop_task->he_connector;
    pthread_mutex_lock(&he_connector->mutex);
    struct zv_he_connector_after_stop_s * after_stop = he_connector->after_stop;
    he_connector->after_stop = NULL;
    pthread_mutex_unlock(&he_connector->mutex);
    _zv_he_connector_call_after_stop(he_connector, after_stop);
    zv_he_connector_release(he_connector);
}


int zv_he_connector_stop(
    zv_he_connector_t * he_connector,
    void (*after_stop_func)(zv_he_connector_t*, void*),
    void * after_stop_arg
) {
    if (after_stop_func == NULL)
    {
        pthread_mutex_lock(&he_connector->mutex);
        he_connector->stop = 1;
        pthread_mutex_unlock(&he_connector->mutex);
        return 0;
    }

    struct zv_he_connector_after_stop_task_s * after_stop_task;
    struct zv_he_connector_after_stop_s * after_stop;

    pthread_mutex_lock(&he_connector->mutex);
    he_connector->stop = 1;

    if ((he_connector->state == zv_he_connector_state_done)
        && (he_connector->after_stop == NULL))
    {
        after_stop_task = malloc(sizeof(struct zv_he_connector_after_stop_task_s));
        if (after_stop_task == NULL)
        {
            pthread_mutex_unlock(&he_connector->mutex);
            return ZV_ERR_NOMEM;
        }
        zv_thread_pool_task_init(&after_stop_task->task, &_zv_he_connector_after_stop_task);
        after_stop_task->he_connector = he_connector;
        after_stop = &after_stop_task->after_stop;
    }
    else
    {
        after_stop_task = NULL;
        after_stop = malloc(sizeof(struct zv_he_connector_after_stop_s));
        if (after_stop == NULL)
        {
            pthread_mutex_unlock(&he_connector->mutex);
            return ZV_ERR_NOMEM;
        }
    }

    after_stop->next = he_connector->after_stop;
    after_stop->func = after_stop_func;
    after_stop->arg = after_stop_arg;
    he_connector->after_stop = after_stop;

    pthread_mutex_unlock(&he_connector->mutex);

    if (after_stop_task != NULL)
    {
        zv_he_connector_retain(he_connector);
        zv_thread_pool_t * thread_pool = zv_loop_get_thread_pool(he_connector->loop);
        zv_thread_pool_dispatch(thread_pool, &after_stop_task->task);
    }

    return 0;
}


zv_he_connector_t * zv_he_connector_retain(
    zv_he_connector_t * he_connector
) {
    atomic_fetch_add(&he_connector->retain_count, 1);
    return he_connector;
}


void zv_he_connector_release(
    zv_he_connector_t * he_connector
) {
    size_t retain_count = atomic_fetch_sub(&he_connector->retain_count, 1) - 1;
    if (retain_count == 0)
    {
        ZV_LOG_DEBUG(
            _zv_he_connector_log,
            "0x%" PRIxPTR ": deinit",
            (intptr_t) he_connector
        );

        assert(he_connector->state == zv_he_connector_state_done);
        assert(he_connector->after_stop == NULL);
        free(he_connector->connect_info);
        pthread_mutex_destroy(&he_connector->mutex);
        free(he_connector);
    }
}
