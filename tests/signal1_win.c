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
#include "zv/semaphore.h"
#include "zv/signal.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

struct handler_s
{
    sem_t sem;
    int _Atomic count;
};

static int handler_init(struct handler_s * handler)
{
    int rc = sem_init(&handler->sem, 0, 0);
    atomic_init(&handler->count, 0);
    return rc;
}

static void handler1(void * thunk)
{
    abort();
}

static void handler2(void * thunk)
{
    struct handler_s * handler = thunk;
    atomic_fetch_add(&handler->count, 1);
    sem_post(&handler->sem);
}

static void handler3(void * thunk)
{
    // Should differ from the handler2, otherwise the linker
    // can fold them into the one function (MSVC /OPT:ICF).
    struct handler_s * handler = thunk;
    atomic_fetch_add(&handler->count, 2);
    sem_post(&handler->sem);
}

static void self_remove_handler(void * thunk)
{
    struct handler_s * handler = thunk;
    const int rc = zv_signal_remove_handler(&self_remove_handler);
    if (rc == 0)
        atomic_fetch_add(&handler->count, 1);
    sem_post(&handler->sem);
}

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr); \
            return -1; \
        } \
    } while (0)

int signal1_child()
{
    struct handler_s h2, h3, srh;
    CHECK(handler_init(&h2) == 0);
    CHECK(handler_init(&h3) == 0);
    CHECK(handler_init(&srh) == 0);

    // only SIGINT, SIGBREAK and SIGTERM are supported
    CHECK(zv_signal_add_handler(SIGSEGV, &handler1, NULL) == ZV_ERR_INVAL);

    // a few handlers for the SIGBREAK
    CHECK(zv_signal_add_handler(SIGBREAK, &handler1, NULL) == 0);
    CHECK(zv_signal_add_handler(SIGBREAK, &handler1, NULL) == ZV_ERR_INVAL);
    CHECK(zv_signal_add_handler(SIGBREAK, &handler2, &h2) == 0);
    CHECK(zv_signal_add_handler(SIGBREAK, &handler3, &h3) == 0);
    CHECK(zv_signal_add_handler(SIGBREAK, &self_remove_handler, &srh) == 0);
    CHECK(zv_signal_remove_handler(&handler1) == 0);
    CHECK(zv_signal_remove_handler(&handler1) == ZV_ERR_INVAL);

    // The process is started with the CREATE_NEW_PROCESS_GROUP,
    // so the event will be delivered only to this process.
    CHECK(GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, GetCurrentProcessId()));
    sem_wait(&h2.sem);
    sem_wait(&h3.sem);
    sem_wait(&srh.sem);

    CHECK(zv_signal_remove_handler(&handler2) == 0);
    CHECK(zv_signal_remove_handler(&handler3) == 0);
    CHECK(zv_signal_remove_handler(&self_remove_handler) == ZV_ERR_INVAL);

    CHECK(atomic_load(&h2.count) == 1);
    CHECK(atomic_load(&h3.count) == 2);
    CHECK(atomic_load(&srh.count) == 1);

    sem_destroy(&h2.sem);
    sem_destroy(&h3.sem);
    sem_destroy(&srh.sem);

    return 0;
}

int signal1()
{
    // GenerateConsoleCtrlEvent() delivers the event to the whole process
    // group, so the test is run in a child process having its own group
    // to not disturb the processes sharing the console.
    char path[MAX_PATH];
    const DWORD path_len = GetModuleFileNameA(NULL, path, sizeof(path));
    CHECK((path_len > 0) && (path_len < sizeof(path)));

    char cmd_line[MAX_PATH + 32];
    snprintf(cmd_line, sizeof(cmd_line), "\"%s\" signal1-child", path);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    CHECK(CreateProcessA(path, cmd_line, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi));

    DWORD exit_code = (DWORD) -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CHECK(exit_code == 0);

    return 0;
}
