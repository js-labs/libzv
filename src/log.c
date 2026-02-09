/*
 * Copyright (C) 2022 Sergey Zubarev, info@js-labs.org
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

#include "zv/atomic.h"
#include "zv/log.h"
#include "zv/os_thread.h"
#include "zv/os_time.h"
#include "zv/semaphore.h"
#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#include <stdarg.h>
#include <sys/time.h>
#elif defined(__APPLE__)
#include <sys/time.h>
#endif

typedef enum
{
    zv_log_event_type_log,
    zv_log_event_type_add_backend,
    zv_log_event_type_remove_backend,
    zv_log_event_type_flush
}
zv_log_event_type_t;


struct zv_log_event_s
{
    struct zv_log_event_s * next;
    zv_log_event_type_t type;

    union
    {
        struct
        {
            const struct zv_log_layer_s * layer;
            zv_log_level_t level;
            pid_t thread_id;
            struct timeval tv;
            const char * file;
            int line;
            size_t text_size;
        }
        log;

        struct
        {
            struct zv_log_backend_s * backend;
        }
        add_backend;

        struct
        {
            struct zv_log_backend_s * backend;
            sem_t sem;
        }
        remove_backend;

        struct
        {
            sem_t sem;
        }
        flush;
    };
};


struct zv_log_backend_s
{
    struct zv_log_backend_s * next;
    void (*func)(struct zv_log_event_s *, void *);
    void * ctx;
};


struct zv_log_s
{
    struct zv_log_backend_s * backend;

    struct zv_log_event_s * queue_head;
    struct zv_log_event_s * queue_tail;

    sem_t sem;
};

static struct zv_log_s * _zv_log = NULL;


static void * _zv_log_worker(void * arg)
{
    struct zv_log_s * log = (struct zv_log_s*) arg;
    for (;;)
    {
        sem_wait(&log->sem);
        struct zv_log_event_s * log_event = log->queue_head;
        do
        {
            if (log_event->type == zv_log_event_type_log)
            {
                struct zv_log_backend_s * backend = log->backend;
                while (backend != NULL)
                {
                    backend->func(log_event, backend->ctx);
                    backend = backend->next;
                }
            }
            else if (log_event->type == zv_log_event_type_add_backend)
            {
                struct zv_log_backend_s * backend = log_event->add_backend.backend;
                backend->next = log->backend;
                log->backend = backend;
            }
            else if (log_event->type == zv_log_event_type_remove_backend)
            {
                struct zv_log_backend_s * prev = NULL;
                struct zv_log_backend_s * backend = log->backend;
                for (;;)
                {
                    if (backend == NULL)
                    {
                        // not found
                        log_event->remove_backend.backend = NULL;
                        break;
                    }

                    if (backend == log_event->remove_backend.backend)
                    {
                        if (prev == NULL)
                            log->backend = backend->next;
                        else
                            prev->next = backend->next;
                        break;
                    }

                    prev = backend;
                    backend = backend->next;
                }
            }
            else if (log_event->type == zv_log_event_type_flush)
            {
                // do nothing
            }

            struct zv_log_event_s * next = zv_atomic_load(&log_event->next);
            if (next == NULL)
            {
                zv_atomic_store(&log->queue_head, NULL);
                struct zv_log_event_s * tail = log_event;
                if (!zv_atomic_cas(&log->queue_tail, &tail, NULL))
                {
                    while ((next = zv_atomic_load(&log_event->next)) == NULL)
                        zv_atomic_wait();
                }
            }

            if ((log_event->type == zv_log_event_type_log)
                || (log_event->type == zv_log_event_type_add_backend))
            {
                 free(log_event);
            }
            else if (log_event->type == zv_log_event_type_remove_backend)
            {
                sem_post(&log_event->remove_backend.sem);
            }
            else if (log_event->type == zv_log_event_type_flush)
            {
                sem_post(&log_event->flush.sem);
            }

            log_event = next;
        }
        while (log_event != NULL);
    }
}


static struct zv_log_s * _zv_log_create()
{
    struct zv_log_s * new_log = malloc(sizeof(struct zv_log_s));
    if (!new_log)
        return NULL;

    new_log->backend = NULL;
    new_log->queue_head = NULL;
    new_log->queue_tail = NULL;
    sem_init(&new_log->sem, 0, 0);

    struct zv_log_s * log = NULL;
    if (zv_atomic_cas(&_zv_log, &log, new_log))
    {
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, &_zv_log_worker, new_log);
        return new_log;
    }
    else
    {
        sem_destroy(&new_log->sem);
        free(new_log);
        return log;
    }
}


const char * zv_log_level_get_name(zv_log_level_t level)
{
    switch (level)
    {
        case zv_log_level_error:
            return "error";

        case zv_log_level_warning:
            return "warning";

        case zv_log_level_info:
            return "info";

        case zv_log_level_debug:
            return "debug";

        case zv_log_level_trace:
            return "trace";

        default:
            return "unknown";
    }
}


zv_log_event_t * zv_log_event_create(
    const struct zv_log_layer_s * layer,
    zv_log_level_t level,
    const char * file,
    int line,
    const char * fmt, ...
) {
    char buffer[1024];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    struct zv_log_event_s * log_event = malloc(sizeof(struct zv_log_event_s) + len + 1);
    log_event->next = NULL;
    log_event->type = zv_log_event_type_log;
    log_event->log.layer = layer;
    log_event->log.level = level;
    log_event->log.thread_id = gettid();
    gettimeofday(&log_event->log.tv, NULL);
    log_event->log.file = file;
    log_event->log.line = line;
    log_event->log.text_size = len;
    char * ptr = (char*) (log_event + 1);
    if (len > sizeof(buffer))
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(ptr, len, fmt, ap);
        va_end(ap);
    }
    else
        memcpy(ptr, buffer, len);

    ptr[len] = 0;

    return log_event;
}


zv_log_backend_t * zv_log_add_backend(void (*func)(zv_log_event_t *, void *), void * ctx)
{
    struct zv_log_backend_s * backend = malloc(sizeof(struct zv_log_backend_s));
    if (backend)
    {
        backend->next = NULL;
        backend->func = func;
        backend->ctx = ctx;

        struct zv_log_event_s * log_event = malloc(sizeof(struct zv_log_event_s));
        if (log_event)
        {
            log_event->next = NULL;
            log_event->type = zv_log_event_type_add_backend;
            log_event->add_backend.backend = backend;
            zv_log(log_event);
            return backend;
        }

        free(backend);
    }
    return NULL;
}


static void _zv_log_file_backend_func(zv_log_event_t * log_event, void * ctx)
{
    char time_str[32];
    struct tm tm;
#if defined(_WIN32)
    const time_t sec = log_event->log.tv.tv_sec;
    localtime_s(&tm, &sec);
#else
    localtime_r(&log_event->log.tv.tv_sec, &tm);
#endif
    //size_t len = strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", &tm);
    size_t len = strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm);
    snprintf(time_str + len, sizeof(time_str) - len, ".%06d", (int) log_event->log.tv.tv_usec);

    FILE * file = (FILE*) ctx;
    const char * text = (const char*) (log_event + 1);
    fprintf(file, "%s <%s> [%d] %s: %s\n",
        time_str,
        zv_log_level_get_name(log_event->log.level),
        log_event->log.thread_id,
        log_event->log.layer->name,
        text
    );
}


zv_log_backend_t * zv_log_add_file_backend(FILE * file)
{
    return zv_log_add_backend(&_zv_log_file_backend_func, file);
}


int zv_log_remove_backend(zv_log_backend_t * backend, void ** pctx)
{
    struct zv_log_event_s log_event;
    log_event.next = NULL;
    log_event.type = zv_log_event_type_remove_backend;
    log_event.remove_backend.backend = backend;
    int rc = sem_init(&log_event.remove_backend.sem, 0, 0);
    if (rc == 0)
    {
        zv_log(&log_event);
        sem_wait(&log_event.remove_backend.sem);
        sem_destroy(&log_event.remove_backend.sem);

        if (log_event.remove_backend.backend != NULL)
        {
            if (pctx)
                *pctx = log_event.remove_backend.backend->ctx;
            free(log_event.remove_backend.backend);
            return 0;
        }
    }
    return -1;
}


FILE * zv_log_remove_file_backend(zv_log_backend_t * backend)
{
    void * ctx;
    int rc = zv_log_remove_backend(backend, &ctx);
    if (rc == 0)
        return (FILE*) ctx;
    else
        return NULL;
}


void zv_log(zv_log_event_t * log_event)
{
    struct zv_log_s * log = zv_atomic_load(&_zv_log);
    if (log == NULL)
        log = _zv_log_create();

    struct zv_log_event_s * tail = zv_atomic_swap(&log->queue_tail, log_event);
    if (tail == NULL)
    {
        zv_atomic_store(&log->queue_head, log_event);
        sem_post(&log->sem);
    }
    else
        zv_atomic_store(&tail->next, log_event);
}


void zv_log_flush()
{
    struct zv_log_s * log = zv_atomic_load(&_zv_log);
    if (log != NULL)
    {
        struct zv_log_event_s log_event;
        log_event.next = NULL;
        log_event.type = zv_log_event_type_flush;
        sem_init(&log_event.flush.sem, 0, 0);
        zv_log(&log_event);
        sem_wait(&log_event.flush.sem);
        sem_destroy(&log_event.flush.sem);
    }
}
