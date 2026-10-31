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

#if !defined(ZV_SIGNAL_H)
#define ZV_SIGNAL_H

#include "zv/export.h"

#if defined(__cplusplus)
extern "C" {
#endif

#include <signal.h>

/*
 * The handler function is called from the internal thread,
 * not from the signal handler context, so it is not limited
 * to the async-signal-safe functions and is allowed to add and
 * remove handlers. A function can be registered only once
 * and identifies the handler for the zv_signal_remove_handler().
 * The handler function is guaranteed to be not running when the
 * zv_signal_remove_handler() returns (except if called from the function itself).
 * Previous signal disposition is restored when the last handler
 * for the signal is removed.
 *
 * Windows: signals are emulated with the console control handler,
 * only the following signals are supported:
 *    SIGINT   - CTRL_C_EVENT
 *    SIGBREAK - CTRL_BREAK_EVENT
 *    SIGTERM  - CTRL_CLOSE_EVENT, CTRL_LOGOFF_EVENT, CTRL_SHUTDOWN_EVENT
 * The handler function is called from the thread created by the system
 * to deliver the event. The system terminates the process as soon as
 * all SIGTERM handlers return (or the system timeout expires).
 */
ZV_EXPORT int zv_signal_add_handler(int signum, void (*func)(void*), void * thunk);
ZV_EXPORT int zv_signal_remove_handler(void (*func)(void*));

#if defined(__cplusplus)
}
#endif

#endif /* !defined(ZV_SIGNAL_H) */
