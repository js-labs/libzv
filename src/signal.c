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

#include "zv/err.h"
#include "zv/signal.h"
#include "zv/os_thread.h"
#include "misc.h"

#include <assert.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct zv_signal_handler_s
{
    struct zv_signal_handler_s * next;
    void (*func)(void*);
    void * thunk;
};


struct zv_signal_group_s
{
    struct zv_signal_group_s * next;
    int signum;
    struct sigaction old_sa;
    struct zv_signal_handler_s * handler;
};


struct zv_signal_s
{
    size_t _Atomic ref_count;
    pthread_cond_t cond;

    struct zv_signal_group_s * group;

    struct
    {
        pthread_t thread;
        struct zv_signal_handler_s * handler;
        struct zv_signal_handler_s * next_handler;
    }
    dispatch;

    int read_fd;
};

static pthread_mutex_t _zv_signal_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct zv_signal_s * _zv_signal = NULL;

static struct zv_signal_write_s
{
    atomic_int lock;
    int fd;
}
_zv_signal_write = { 0, -1 };


static int _zv_signal_set_write_fd(int write_fd)
{
    while (atomic_exchange(&_zv_signal_write.lock, 1) == 1)
        _zv_atomic_wait();
    int prev_write_fd = _zv_signal_write.fd;
    _zv_signal_write.fd = write_fd;
    atomic_exchange(&_zv_signal_write.lock, 0);
    return prev_write_fd;
}


static void _zv_signal_reset_write_fd()
{
    int write_fd = _zv_signal_set_write_fd(-1);
    if (write_fd >= 0)
        close(write_fd);
    else
        assert(0);
}


static void _zv_signal_release(struct zv_signal_s * signal)
{
    size_t ref_count = (atomic_fetch_sub(&signal->ref_count, 1) - 1);
    if (ref_count == 0)
    {
        pthread_cond_destroy(&signal->cond);
        close(signal->read_fd);
        free(signal);
    }
}


static void _zv_signal_sys_handler(int signum)
{
    int saved_errno = errno;
    while (atomic_exchange(&_zv_signal_write.lock, 1) == 1)
        _zv_atomic_wait();
    int write_fd = _zv_signal_write.fd;
    if (write_fd >= 0)
    {
        unsigned char c = (unsigned char) signum;
        ssize_t rc = write(write_fd, &c, sizeof(c));
        (void) rc;
    }
    atomic_store(&_zv_signal_write.lock, 0);
    errno = saved_errno;
}


static void _zv_signal_dispatch_locked(
    struct zv_signal_s * signal,
    struct zv_signal_group_s * group
) {
    struct zv_signal_handler_s * handler = group->handler;
    signal->dispatch.thread = pthread_self();
    while (handler != NULL)
    {
        signal->dispatch.handler = handler;
        signal->dispatch.next_handler = handler->next;
        void (*func)(void*) = handler->func;
        void * thunk = handler->thunk;
        pthread_mutex_unlock(&_zv_signal_mutex);
        func(thunk);
        pthread_mutex_lock(&_zv_signal_mutex);
        pthread_cond_broadcast(&signal->cond);
        handler = signal->dispatch.next_handler;
    }
    signal->dispatch.handler = NULL;
}


static void * _zv_signal_worker(void * arg)
{
    struct zv_signal_s * signal = arg;

    for (;;)
    {
        unsigned char signum;
        ssize_t bytes_read = read(signal->read_fd, &signum, sizeof(signum));
        if (bytes_read == 0)
            break;
        else if (bytes_read < 0)
        {
            if (errno == EINTR)
                continue;
            else
                break;
        }

        pthread_mutex_lock(&_zv_signal_mutex);

        struct zv_signal_group_s * group = signal->group;
        while (group != NULL)
        {
            if (group->signum == signum)
            {
                _zv_signal_dispatch_locked(signal, group);
                break;
            }
            group = group->next;
        }

        pthread_mutex_unlock(&_zv_signal_mutex);
    }

    _zv_signal_release(signal);

    return NULL;
}


static int _zv_signal_create(struct zv_signal_s ** psignal)
{
    struct zv_signal_s * signal = malloc(sizeof(*signal));
    if (signal == NULL)
        return ZV_ERR_NOMEM;

    signal->ref_count = 2;
    int rc = pthread_cond_init(&signal->cond, NULL);
    if (rc == 0)
    {
        int pipefd[2];
        rc = pipe(pipefd);
        if (rc == 0)
        {
            // The signal handler should never block on the write.
            int write_fd_flags = fcntl(pipefd[1], F_GETFL);
            if (write_fd_flags >= 0)
                fcntl(pipefd[1], F_SETFL, write_fd_flags|O_NONBLOCK);

            fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
            fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

            sigset_t sigset;
            sigfillset(&sigset);
            sigdelset(&sigset, SIGSEGV);
            sigdelset(&sigset, SIGBUS);
            sigdelset(&sigset, SIGFPE);
            sigdelset(&sigset, SIGILL);

            sigset_t prev_sigset;
            rc = pthread_sigmask(SIG_BLOCK, &sigset, &prev_sigset);
            if (rc == 0)
            {
                signal->group = NULL;
                signal->dispatch.handler = NULL;
                signal->dispatch.next_handler = NULL;
                signal->read_fd = pipefd[0];

                pthread_t worker_thread;
                rc = pthread_create(&worker_thread, NULL, &_zv_signal_worker, signal);
                pthread_sigmask(SIG_SETMASK, &prev_sigset, NULL);
                if (rc == 0)
                {
                    pthread_detach(worker_thread);
                    int write_fd = _zv_signal_set_write_fd(pipefd[1]);
                    if (write_fd >= 0)
                    {
                        close(write_fd);
                        assert(0);
                    }
                    *psignal = signal;
                    return 0;
                }
            }

            close(pipefd[0]);
            close(pipefd[1]);
        }
        pthread_cond_destroy(&signal->cond);
    }
    free(signal);
    return rc;
}


int zv_signal_add_handler(int signum, void (*func)(void*), void * thunk)
{
    if ((signum <= 0) || (signum >= NSIG) || (func == NULL))
        return ZV_ERR_INVAL;

    pthread_mutex_lock(&_zv_signal_mutex);
    struct zv_signal_s * signal = _zv_signal;

    if (signal == NULL)
    {
        int rc = _zv_signal_create(&signal);
        if (rc != 0)
        {
            pthread_mutex_unlock(&_zv_signal_mutex);
            return rc;
        }
    }
    else
    {
        struct zv_signal_group_s * group = signal->group;
        while (group != NULL)
        {
            struct zv_signal_handler_s * handler = group->handler;
            while (handler != NULL)
            {
                if (handler->func == func)
                {
                    pthread_mutex_unlock(&_zv_signal_mutex);
                    return ZV_ERR_INVAL;
                }
                handler = handler->next;
            }
            group = group->next;
        }
    }

    struct zv_signal_group_s * group = signal->group;
    while (group != NULL)
    {
        if (group->signum == signum)
        {
            struct zv_signal_handler_s * handler = malloc(sizeof(*handler));
            if (handler == NULL)
            {
                pthread_mutex_unlock(&_zv_signal_mutex);
                return ZV_ERR_NOMEM;
            }
            else
            {
                handler->next = group->handler;
                handler->func = func;
                handler->thunk = thunk;
                group->handler = handler;
                pthread_mutex_unlock(&_zv_signal_mutex);
                return 0;
            }
        }
        group = group->next;
    }

    // new group

    size_t malloc_size = sizeof(struct zv_signal_group_s) + sizeof(struct zv_signal_handler_s);
    group = malloc(malloc_size);
    if (group == NULL)
    {
        if (_zv_signal == NULL)
            _zv_signal_reset_write_fd();
        else
            signal = NULL;
        pthread_mutex_unlock(&_zv_signal_mutex);

        if (signal != NULL)
            _zv_signal_release(signal);

        return ZV_ERR_NOMEM;
    }

    struct zv_signal_handler_s * handler = (void*) (group + 1);

    handler->next = NULL;
    handler->func = func;
    handler->thunk = thunk;

    group->next = signal->group;
    group->signum = signum;
    group->handler = handler;
    signal->group = group;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &_zv_signal_sys_handler;
    sa.sa_flags = SA_RESTART;
    int rc = sigaction(signum, &sa, &group->old_sa);
    if (rc == 0)
    {
        _zv_signal = signal;
        pthread_mutex_unlock(&_zv_signal_mutex);
        return 0;
    }
    else
    {
        signal->group = group->next;
        if (_zv_signal == NULL)
            _zv_signal_reset_write_fd();
        else
            signal = NULL;
        pthread_mutex_unlock(&_zv_signal_mutex);

        free(group);

        if (signal != NULL)
            _zv_signal_release(signal);

        return ZV_ERR_INVAL;
    }
}


int zv_signal_remove_handler(void (*func)(void*))
{
    pthread_mutex_lock(&_zv_signal_mutex);

    struct zv_signal_s * signal = _zv_signal;
    if (signal == NULL)
    {
        pthread_mutex_unlock(&_zv_signal_mutex);
        return ZV_ERR_INVAL;
    }

    struct zv_signal_group_s ** group_link = &signal->group;
    struct zv_signal_group_s * group = signal->group;
    struct zv_signal_handler_s ** handler_link;
    struct zv_signal_handler_s * handler;
    for (;;)
    {
        while (group != NULL)
        {
            handler_link = &group->handler;
            handler = group->handler;
            while (handler != NULL)
            {
                if (handler->func == func)
                    goto found;
                handler_link = &handler->next;
                handler = handler->next;
            }
            group_link = &group->next;
            group = group->next;
        }

        pthread_mutex_unlock(&_zv_signal_mutex);
        return ZV_ERR_INVAL;

    found:
        if (handler == signal->dispatch.handler)
        {
            if (pthread_equal(signal->dispatch.thread, pthread_self()))
            {
                // Called from the handler callback, continue
                break;
            }
            else
            {
                // The callback is running, wait until it is done
                pthread_cond_wait(&signal->cond, &_zv_signal_mutex);
                if (_zv_signal != signal)
                {
                    pthread_mutex_unlock(&_zv_signal_mutex);
                    return ZV_ERR_INVAL;
                }
                group_link = &signal->group;
                group = signal->group;
            }
        }
        else
            break;
    }

    *handler_link = handler->next;
    if (handler == (void*) (group + 1))
    {
        if (group->handler == NULL)
        {
            sigaction(group->signum, &group->old_sa, NULL);
            *group_link = group->next;
            free(group);
            if (signal->group == NULL)
            {
                _zv_signal = NULL;
                _zv_signal_reset_write_fd();
                pthread_mutex_unlock(&_zv_signal_mutex);
                _zv_signal_release(signal);
                return 0;
            }
        }
        else
        {
            if (signal->dispatch.next_handler == handler)
                signal->dispatch.next_handler = handler->next;
            else if (signal->dispatch.next_handler == group->handler)
                signal->dispatch.next_handler = handler;
            *handler = *group->handler;
            if (signal->dispatch.handler == handler)
                signal->dispatch.handler = NULL;
            else if (signal->dispatch.handler == group->handler)
                signal->dispatch.handler = handler;
            free(group->handler);
            group->handler = handler;
        }
    }
    else
    {
        if (signal->dispatch.next_handler == handler)
            signal->dispatch.next_handler = handler->next;
        free(handler);
    }

    pthread_mutex_unlock(&_zv_signal_mutex);

    return 0;
}
