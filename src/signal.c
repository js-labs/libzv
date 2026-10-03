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

#include <assert.h>
#if defined(_WIN32)
#include <Windows.h>
#endif

#define REMOVED ((struct zv_signal_handler_s*)(uintptr_t)1)

struct zv_signal_handler_s
{
    int signum;
    void (*func)(void*);
    void * thunk;
    struct zv_signal_handler_s * next;
};


struct zv_signal_s
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    struct zv_signal_handler_s * handlers[2];
    struct zv_signal_handler_s * dispatch_handler;
    pthread_t dispatch_thread;
};


static pthread_once_t _zv_signal_initialized = PTHREAD_ONCE_INIT;
static struct zv_signal_s _zv_signal;

static size_t _zv_signal_dispatch(int signum)
{
    size_t dispatched_handlers = 0;

    pthread_mutex_lock(&_zv_signal.mutex);

    while (_zv_signal.dispatch_handler != NULL)
        pthread_cond_wait(&_zv_signal.cond, &_zv_signal.mutex);

    struct zv_signal_handler_s * handler = _zv_signal.handlers[0];
    if (handler != NULL)
    {
        do
        {
            if (handler->signum == signum)
            {
                _zv_signal.handlers[0] = handler->next;
                handler->next = NULL;
                _zv_signal.dispatch_handler = handler;
                _zv_signal.dispatch_thread = pthread_self();
                pthread_mutex_unlock(&_zv_signal.mutex);

                handler->func(handler->thunk);

                pthread_mutex_lock(&_zv_signal.mutex);

                dispatched_handlers++;
                if (_zv_signal.dispatch_handler == handler)
                {
                    handler->next = _zv_signal.handlers[1];
                    _zv_signal.handlers[1] = handler;
                }
                else
                {
                    // zv_signal_remove_handler() called from the callback
                    assert(_zv_signal.dispatch_handler == REMOVED);
                }
            }
            else
            {
                _zv_signal.handlers[0] = handler->next;
                handler->next = _zv_signal.handlers[1];
                _zv_signal.handlers[1] = handler;
            }
            handler = _zv_signal.handlers[0];
        }
        while (handler != NULL);

        _zv_signal.handlers[0] = _zv_signal.handlers[1];
        _zv_signal.handlers[1] = NULL;
        _zv_signal.dispatch_handler = NULL;

        pthread_cond_broadcast(&_zv_signal.cond);
    }
    pthread_mutex_unlock(&_zv_signal.mutex);
    return dispatched_handlers;
}


#if defined(_WIN32)

static BOOL WINAPI _zv_console_handler(DWORD signal)
{
    size_t dispatched_handlers = 0;
    switch (signal)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        dispatched_handlers = _zv_signal_dispatch(SIGINT);
        break;

    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        dispatched_handlers = _zv_signal_dispatch(SIGTERM);
        break;
    }
    return (dispatched_handlers > 0) ? TRUE : FALSE;
}

static int _zv_signal_has_handlers_locked(void)
{
    return (_zv_signal.handlers[0] != NULL)
        || (_zv_signal.handlers[1] != NULL)
        || ((_zv_signal.dispatch_handler != NULL) && (_zv_signal.dispatch_handler != REMOVED));
}

#endif

static void _zv_signal_init(void)
{
    int rc = pthread_mutex_init(&_zv_signal.mutex, NULL);
    if (rc == 0)
    {
        rc = pthread_cond_init(&_zv_signal.cond, NULL);
        if (rc == 0)
        {
            _zv_signal.handlers[0] = NULL;
            _zv_signal.handlers[1] = NULL;
            _zv_signal.dispatch_handler = NULL;
            // _zv_signal.dispatch_thread
        }
        else
            abort();
    }
    else
        abort();
}


int zv_signal_add_handler(int signum, void (*func)(void*), void * thunk)
{
#if defined(_WIN32)
    if ((signum != SIGINT) && (signum != SIGTERM))
        return ZV_ERR_INVAL;
#endif

    pthread_once(&_zv_signal_initialized, &_zv_signal_init);

    struct zv_signal_handler_s * new_handler = malloc(sizeof(struct zv_signal_handler_s));
    if (new_handler == NULL)
        return ZV_ERR_NOMEM;

    pthread_mutex_lock(&_zv_signal.mutex);

#if !defined(_WIN32)
    int install_system_handler = 1;
#endif

    for (size_t idx=0; idx<2; idx++)
    {
        struct zv_signal_handler_s * handler = _zv_signal.handlers[idx];
        while (handler != NULL)
        {
#if !defined(_WIN32)
            if (handler->signum == signum)
                install_system_handler = 0;
#endif
            if (handler->func == func)
            {
                pthread_mutex_unlock(&_zv_signal.mutex);
                free(new_handler);
                return ZV_ERR_INVAL;
            }
            handler = handler->next;
        }
    }

    if ((_zv_signal.dispatch_handler != NULL)
        && (_zv_signal.dispatch_handler != REMOVED))
    {
#if !defined(_WIN32)
        if (_zv_signal.dispatch_handler->signum == signum)
            install_system_handler = 0;
#endif
        if (_zv_signal.dispatch_handler->func == func)
        {
            pthread_mutex_unlock(&_zv_signal.mutex);
            free(new_handler);
            return ZV_ERR_INVAL;
        }
    }

    new_handler->signum = signum;
    new_handler->func = func;
    new_handler->thunk = thunk;
    new_handler->next = _zv_signal.handlers[0];

#if defined(_WIN32)
    if (!_zv_signal_has_handlers_locked())
        SetConsoleCtrlHandler(&_zv_console_handler, TRUE);
#else
#endif

    _zv_signal.handlers[0] = new_handler;

    pthread_mutex_unlock(&_zv_signal.mutex);

    return 0;
}


static struct zv_signal_handler_s * _zv_signal_remove_handler(
    struct zv_signal_handler_s * handler,
    void (*func)(void*),
    struct zv_signal_handler_s ** removed
) {
    struct zv_signal_handler_s * head = handler;
    struct zv_signal_handler_s * prev = NULL;
    while (handler != NULL)
    {
        if (handler->func == func)
        {
            if (prev == NULL)
                head = handler->next;
            else
                prev->next = handler->next;
            handler->next = NULL;
            *removed = handler;
            break;
        }
        prev = handler;
        handler = handler->next;
    }
    return head;
}

#if !defined(_WIN32)

static struct zv_signal_handler_s * _zv_signal_find_handler(
    struct zv_signal_handler_s * handler,
    int signum
) {
    while (handler != NULL)
    {
        if (handler->signum == signum)
            return handler;
        handler = handler->next;
    }
    return NULL;
}

#endif

int zv_signal_remove_handler(void (*func)(void*))
{
    pthread_once(&_zv_signal_initialized, &_zv_signal_init);

    struct zv_signal_handler_s * removed = NULL;

    pthread_mutex_lock(&_zv_signal.mutex);

    if (_zv_signal.dispatch_handler == NULL)
    {
        assert(_zv_signal.handlers[1] == NULL);
        _zv_signal.handlers[0] = _zv_signal_remove_handler(_zv_signal.handlers[0], func, &removed);
    }
    else if (pthread_equal(_zv_signal.dispatch_thread, pthread_self()))
    {
        // called from the callback
        if ((_zv_signal.dispatch_handler == REMOVED) || (_zv_signal.dispatch_handler->func != func))
        {
            // removing different handler
            _zv_signal.handlers[0] = _zv_signal_remove_handler(_zv_signal.handlers[0], func, &removed);
            if (removed == NULL)
                _zv_signal.handlers[1] = _zv_signal_remove_handler(_zv_signal.handlers[1], func, &removed);
        }
        else // (_zv_signal.dispatch_handler->func == func)
        {
            // removing same handler
            removed = _zv_signal.dispatch_handler;
            _zv_signal.dispatch_handler = REMOVED;
        }
    }
    else
    {
        for (;;)
        {
            pthread_cond_wait(&_zv_signal.cond, &_zv_signal.mutex);
            if (_zv_signal.dispatch_handler == NULL)
                break;
        }
        assert(_zv_signal.handlers[1] == NULL);
        _zv_signal.handlers[0] = _zv_signal_remove_handler(_zv_signal.handlers[0], func, &removed);
    }

    if (removed == NULL)
    {
        pthread_mutex_unlock(&_zv_signal.mutex);
        return ZV_ERR_INVAL;
    }

#if defined(_WIN32)
    if (!_zv_signal_has_handlers_locked())
        SetConsoleCtrlHandler(&_zv_console_handler, FALSE);
#else
#endif

    pthread_mutex_unlock(&_zv_signal.mutex);

    free(removed);

    return 0;
}
