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

#include "zv/buffer.h"
#include "zv/channel.h"
#include "zv/log.h"
#include "zv/loop.h"
#include "zv/thread_pool.h"
#include "loop_int.h"
#include "misc.h"

#include "channel_in_queue.h"
#include "channel_out_queue.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(__INTELLISENSE__)
#define _Atomic
#else
#include <stdatomic.h>
#endif

#if defined(_WIN32)

#include <WS2tcpip.h>

#elif defined(__linux__) || defined(__APPLE__)

#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/uio.h>

#endif


struct zv_log_layer_s _zv_channel_log = { "xv.channel", zv_log_level_warning };


struct zv_channel_s
{
    zv_loop_t * loop;
    zv_thread_pool_t * thread_pool;

    socklen_t local_addr_len;
    char * local_addr_str;

    socklen_t peer_addr_len;
    char * peer_addr_str;

    _Atomic size_t retain_count;
    pthread_key_t tls_iov;
    size_t iov_max;

    struct zv_event_handler_s read_handler;
#if defined(_ZV_HAS_RDTSC)
    struct zv_channel_read_stats_s read_stats;
#endif
    struct zv_channel_in_queue_s in_queue;

    struct zv_event_handler_s write_handler;
    struct zv_channel_out_queue_s out_queue;

    void (*input_handler)(zv_channel_t*, const void *, size_t);
    void (*close_handler)(zv_channel_t*);
    void * extra_data;
};


static inline struct zv_channel_s * _zv_channel_by_read_task(
    struct zv_thread_pool_task_s * task
) {
    return _ZV_CONTAINER_OF(
        _ZV_CONTAINER_OF(task, struct zv_event_handler_s, task),
        struct zv_channel_s,
        read_handler
    );
}


static inline struct zv_channel_s * _zv_channel_by_write_task(
    struct zv_thread_pool_task_s * task
) {
    return _ZV_CONTAINER_OF(
        _ZV_CONTAINER_OF(task, struct zv_event_handler_s, task),
        struct zv_channel_s,
        write_handler
    );
}


struct zv_channel_stop_read_s
{
    struct zv_channel_s * channel;
    struct zv_event_handler_s handler;
};


static void _zv_channel_after_remove(struct zv_thread_pool_task_s * task)
{
    struct zv_channel_s * channel = _zv_channel_by_read_task(task);
    zv_channel_release(channel);
}


static void _zv_channel_should_never_be_called(struct zv_thread_pool_task_s * task)
{
    struct zv_channel_s * channel = _zv_channel_by_read_task(task);
    ZV_LOG_ERROR(
        _zv_channel_log,
        "0x%" PRIxPTR ": internal error: should never be called",
        channel
    );
    assert(0);
}


#if defined(_ZV_HAS_RDTSC)
static void _zv_channel_read_task_i(struct zv_thread_pool_task_s * task)
#else
static void _zv_channel_read_task(struct zv_thread_pool_task_s * task)
#endif
{
    struct zv_channel_s * channel = _zv_channel_by_read_task(task);

    struct iovec iov[2];
    zv_channel_in_queue_get_wr(&channel->in_queue, iov);

#if defined(_WIN32)
    DWORD bytes_received;
    DWORD flags = 0;
    int rc = WSARecv(channel->read_handler.sockfd, iov, 2, &bytes_received, &flags, NULL, NULL);
    if (rc == SOCKET_ERROR)
        bytes_received = 0;
#else
    ssize_t bytes_received = readv(channel->read_handler.sockfd, iov, 2);
#endif

    if (bytes_received > 0)
    {
        int suspend;
        size_t queue_size = zv_channel_in_queue_move_wr(&channel->in_queue, bytes_received, &suspend);

        ZV_LOG_TRACE(
            _zv_channel_log,
            "0x%" PRIxPTR ": read_task: bytes_received=%zu queue_size=%zu suspend=%d",
            channel, bytes_received, queue_size, suspend
        );

        if (suspend)
        {
            // do not resume read event handler now, will do it later
            // when the queue size will be less than low watermark
        }
        else
        {
            channel->read_handler.opcode = zv_event_opcode_resume;
            _zv_loop_resume_handler(channel->loop, &channel->read_handler);
        }

        if (queue_size == bytes_received)
        {
            size_t bytes_ready = queue_size;
            for (;;)
            {
                int resume_action;
                bytes_ready = zv_channel_in_queue_process_data(
                    &channel->in_queue,
                    bytes_ready,
                    channel->input_handler,
                    channel,
                    &resume_action
                );

                if (zv_log_level_enabled(&_zv_channel_log, zv_log_level_trace))
                {
                    char buffer[32];
                    int offs;
                    char * queue_state_str;
                    if (bytes_ready == SIZE_MAX)
                    {
                        queue_state_str = " closed";
                        offs = 0;
                    }
                    else
                    {
                        queue_state_str = buffer;
                        offs = snprintf(buffer, sizeof(buffer), "bytes_ready=%zu", bytes_ready);
                    }

                    char * resume_action_str;
                    if (resume_action == 0)
                        resume_action_str = "<nil>";
                    else
                    {
                        resume_action_str = (buffer + offs + 1);
                        snprintf(resume_action_str, sizeof(buffer) - offs, "%d", (resume_action & 0xFF));
                    }

                    ZV_LOG_TRACE(
                        _zv_channel_log,
                        "0x%" PRIxPTR ": read_task: %s, resume_action=%s",
                        channel, queue_state_str, resume_action_str
                    );
                }

                if (resume_action != 0)
                {
                    resume_action &= 0xFF;
                    channel->read_handler.opcode = resume_action;
                    _zv_loop_resume_handler(channel->loop, &channel->read_handler);
                }

                if (bytes_ready == SIZE_MAX)
                {
                    // queue is empty and closed
                    channel->close_handler(channel);
                    zv_channel_release(channel);
                    return;
                }
                else if (bytes_ready == 0)
                    return;
                // else some new data appeared in the input queue
            }
        }
        // else worth to try to read socket once more?
    }
    else
    {
#if defined(_WIN32)
        int last_error = _zv_get_errno();
        if (last_error == WSAEWOULDBLOCK)
        {
            channel->read_handler.opcode = zv_event_opcode_resume;
            _zv_loop_resume_handler(channel->loop, &channel->read_handler);
            return;
        }
#endif

        int is_empty;
        int close_rc = zv_channel_out_queue_close(&channel->out_queue, &is_empty);

        ZV_LOG_DEBUG(
            _zv_channel_log,
            "0x%" PRIxPTR ": read_task: out_queue: close_rc=%d, is_empty=%d",
            channel, close_rc, is_empty
        );

        if (close_rc < 0)
            ; // already closed, do nothing
        else
        {
            if (is_empty)
                zv_channel_release(channel);
            else
            {
                // outbound queue was not empty, 'close' flag is set
                // channel will be released by the writer task
                // when queue will be empty
            }
        }

        int is_stopped;
        is_empty = zv_channel_in_queue_close(&channel->in_queue, &is_stopped);

        ZV_LOG_DEBUG(
            _zv_channel_log,
            "0x%" PRIxPTR ": read_task: in_queue: is_empty=%d is_stopped=%d",
            channel, is_empty, is_stopped
        );

        if (is_empty)
        {
            channel->close_handler(channel);
            zv_channel_release(channel);
        }

        if (is_stopped)
        {
            channel->read_handler.opcode = zv_event_opcode_resume_wait_remove;
            channel->read_handler.task.func = &_zv_channel_should_never_be_called;
        }
        else
        {
            channel->read_handler.opcode = zv_event_opcode_resume_remove;
            channel->read_handler.task.func = &_zv_channel_after_remove;
        }

        _zv_loop_resume_handler(channel->loop, &channel->read_handler);
    }
}


#if defined(_ZV_HAS_RDTSC)

void _zv_channel_read(
    struct zv_event_handler_s * read_handler,
    struct zv_thread_pool_s * thread_pool
) {
    struct zv_channel_s * channel = _ZV_CONTAINER_OF(read_handler, struct zv_channel_s, read_handler);
    struct zv_channel_read_stats_s * read_stats = &channel->read_stats;
    if ((read_stats->dispatch.count >= 10) &&
        (read_stats->dispatch.value > read_stats->read.value))
    {
        read_stats->dispatch_tsc = 0;
        read_handler->task.func(&read_handler->task);
    }
    else
    {
        read_stats->dispatch_tsc = _zv_rdtsc();
        zv_thread_pool_dispatch(thread_pool, &read_handler->task);
    }
}

static void _zv_channel_read_task(struct zv_thread_pool_task_s * task)
{
    struct zv_channel_s * channel = _zv_channel_by_read_task(task);
    struct zv_channel_read_stats_s * read_stats = &channel->read_stats;
    uint64_t start_tsc = _zv_rdtsc();
    read_stats->total_reads++;

    uint64_t dispatch_tsc = read_stats->dispatch_tsc;
    if (dispatch_tsc != 0)
    {
        uint64_t elapsed_ticks = (start_tsc - dispatch_tsc);
        _zv_mavg_append(&read_stats->dispatch, elapsed_ticks);
        read_stats->async_reads++;
    }

    _zv_channel_read_task_i(task);

    uint64_t elapsed_ticks = (_zv_rdtsc() - start_tsc);
    _zv_mavg_append(&read_stats->read, elapsed_ticks);
}
#endif /* defined(_ZV_HAS_RDTSC) */


static void _zv_channel_write_task(struct zv_thread_pool_task_s * task)
{
    struct zv_channel_s * channel = _zv_channel_by_write_task(task);
    struct zv_channel_out_queue_s * out_queue = &channel->out_queue;

    size_t iov_max = channel->iov_max;
    struct iovec * iov = pthread_getspecific(channel->tls_iov);
    if (iov == NULL)
    {
        iov = malloc(sizeof(struct iovec) * iov_max);
        pthread_setspecific(channel->tls_iov, iov);
    }
    size_t iovc = zv_channel_out_queue_get_data(out_queue, iov, iov_max);
    assert(iovc > 0);

#if defined(_WIN32)
    DWORD bytes_sent;
    int rc = WSASend(channel->write_handler.sockfd, iov, (DWORD)iovc, &bytes_sent, 0, NULL, NULL);
    if (rc != 0)
        bytes_sent = 0;
#elif defined(__linux__)
    // Using sendmsg() on Linux so that there is no need
    // to process the SIGPIPE signal if the socket is closed by peer.
    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = iovc;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;
    ssize_t bytes_sent = sendmsg(channel->write_handler.sockfd, &msg, MSG_NOSIGNAL);
#else
    ssize_t bytes_sent = writev(channel->write_handler.sockfd, iov, iovc);
#endif
    if (bytes_sent > 0)
    {
    #if defined(CHANNEL_SEND_REF)
        size_t blocks_remaining = zv_channel_out_queue_remove(out_queue, bytes_sent);
        if (blocks_remaining == SIZE_MAX)
        {
            // queue is empty and closed
            zv_channel_release(channel);
        }
        else if (blocks_remaining > 0)
        {
            // queue is not empty,
            // but let's write remaining data later
            // allowing other tasks in the thread pool do their job
            zv_thread_pool_dispatch(channel->thread_pool, &channel->write_handler.task);
        }
        else
        {
            // do nothing
        }
    #else
        size_t bytes_remaining = zv_channel_out_queue_remove(out_queue, bytes_sent);
        if (bytes_remaining == SIZE_MAX)
        {
            // queue is empty and closed
            zv_channel_release(channel);
        }
        else if (bytes_remaining > 0)
        {
            // queue is not empty,
            // but let's write remaining data later
            // allowing other tasks in the thread pool do their job
            zv_thread_pool_dispatch(channel->thread_pool, &channel->write_handler.task);
        }
        else
        {
            // do nothing
        }
    #endif
    }
    else
    {
        int last_error = _zv_get_errno();
#if defined(_WIN32)
        if (last_error == WSAEWOULDBLOCK)
#else
        if (last_error == EWOULDBLOCK)
#endif
        {
            _zv_loop_add_handler(channel->loop, &channel->write_handler);
        }
        else
        {
            // There was an error writing data to the socket - most likely
            // the socket is being closed. Let's close the outbound queue
            // for the channel. The inbound queue will be closed soon,
            // when the next read operation will fail.
            int is_empty;
            int rc = zv_channel_out_queue_close(&channel->out_queue, &is_empty);

            ZV_LOG_DEBUG(
                _zv_channel_log,
                "0x%" PRIxPTR ": write() failed (%d), close_rc=%d, is_empty=%d",
                channel, last_error, rc, is_empty
            );

            if (rc < 0)
            {
                // already closed,
                // can happen if zv_channel_stop() called earlier
                if (is_empty)
                {
                    // the channel was released when the queue was closed
                    // do nothing
                }
                else
                {
                    // queue was not empty when it was closed,
                    // it was expected the writer task will send the remainining data,
                    // so the channel should has extra reference count
                    // which can be released now
                    zv_channel_release(channel);
                }
            }
            else
            {
                // it can be empty or not,
                // but it does not matter at that point.
                zv_channel_release(channel);
            }
        }
    }
}


zv_channel_t * zv_channel_create(
    zv_loop_t * loop,
    zv_buffer_cache_t * buffer_cache,
    zv_socket_t sockfd,
    void (*input_handler)(zv_channel_t*, const void*, size_t),
    void (*close_handler)(zv_channel_t*),
    void * extra_data
) {
    struct sockaddr_storage local_addr_storage;
    socklen_t local_addr_len = sizeof(local_addr_storage);
    struct sockaddr * local_addr = (struct sockaddr*) &local_addr_storage;
    int rc = getsockname(sockfd, local_addr, &local_addr_len);
    if (rc != 0)
        return NULL;

    struct sockaddr_storage peer_addr_storage;
    socklen_t peer_addr_len = sizeof(peer_addr_storage);
    struct sockaddr * peer_addr = (struct sockaddr*) &peer_addr_storage;
    rc = getpeername(sockfd, peer_addr, &peer_addr_len);
    if (rc != 0)
        return NULL;

#if defined(_WIN32)
    DWORD nodelay = 1;
    rc = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
#else
    int nodelay = 1;
    rc = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
#endif
    if (rc != 0)
    {
        // sometimes failes on Windows not clear why
        ZV_LOG_ERROR(
            _zv_channel_log,
            "setsockopt(%" PRIsocket ", TCP_NODELAY) failed: %d",
            sockfd,
            _zv_get_errno()
        );
        return NULL;
    }

#if defined(__APPLE__)
    // The OS generates SIGPIPE if we try to write some data to the socket
    // which is being closed. We are going to handle such error via return code,
    // so going to disable signal for such cases.
    // SO_NOSIGPIPE is not supported on Linux,
    // will use MSG_NOSIGNAL send() flag instead.
    int nosigpipe = 1;
    rc = setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
    if (rc != 0)
        return NULL;
#endif

    const size_t channel_size = _zv_round(sizeof(struct zv_channel_s), sizeof(void*));
    const size_t rounded_local_addr_len = _zv_round(local_addr_len, sizeof(void*));

    struct zv_channel_s * channel = malloc(channel_size + rounded_local_addr_len + peer_addr_len);
    if (channel == NULL)
        return NULL;

    char buffer[INET6_ADDRSTRLEN + 6];

    channel->loop = loop;
    channel->thread_pool = loop->thread_pool;

    channel->local_addr_len = local_addr_len;
    zv_sockaddr_ntop(local_addr, local_addr_len, buffer, sizeof(buffer));
    channel->local_addr_str = strdup(buffer);

    channel->peer_addr_len = peer_addr_len;
    zv_sockaddr_ntop(peer_addr, peer_addr_len, buffer, sizeof(buffer));
    channel->peer_addr_str = strdup(buffer);

    channel->retain_count = 1;
    channel->tls_iov = loop->tls_iov;
    channel->iov_max = loop->config.channel.iov_max;

    _zv_event_handler_init(
        &channel->read_handler,
        sockfd,
        zv_event_type_read,
        zv_event_opcode_add,
        &_zv_channel_read_task
    );

#if defined(_ZV_HAS_RDTSC)
    _zv_channel_read_stats_init(&channel->read_stats);
#endif

    // Both inbound and outbound queues use the same buffer cache,
    // but we could retain it only once.
    zv_buffer_cache_retain(buffer_cache);

    zv_channel_in_queue_init(
        &channel->in_queue,
        &loop->config.channel.in_queue,
        buffer_cache
    );

    _zv_event_handler_init(
        &channel->write_handler,
        sockfd,
        zv_event_type_write,
        zv_event_opcode_add,
        &_zv_channel_write_task
    );

    zv_channel_out_queue_init(&channel->out_queue, buffer_cache);

    channel->input_handler = input_handler;
    channel->close_handler = close_handler;
    channel->extra_data = extra_data;

    char * dst = (((char*)channel) + channel_size);
    memcpy(dst, &local_addr_storage, local_addr_len);

    dst += rounded_local_addr_len;
    memcpy(dst, &peer_addr_storage, peer_addr_len);

    ZV_LOG_DEBUG(
        _zv_channel_log,
        "0x%" PRIxPTR ": sockfd=%" PRIsocket ", %s -> %s",
        channel, sockfd, channel->local_addr_str, channel->peer_addr_str
    );

    return channel;
}


void zv_channel_start(zv_channel_t * channel)
{
    // We need 3 extra reference count:
    // 2 for the inbound processor and 1 for outbound
    atomic_fetch_add(&channel->retain_count, 3);
    // _zv_loop_add_handler() can't fail for 'zv_event_type_read'
    /* int rc = */ _zv_loop_add_handler(channel->loop, &channel->read_handler);
}


static void _zv_channel_stop_task(struct zv_thread_pool_task_s * task)
{
    struct zv_channel_stop_read_s * stop_read = _ZV_CONTAINER_OF(
        _ZV_CONTAINER_OF(task, struct zv_event_handler_s, task),
        struct zv_channel_stop_read_s,
        handler
    );
    struct zv_channel_s * channel = stop_read->channel;
    free(stop_read);

    int is_empty;
    int rc = zv_channel_out_queue_close(&channel->out_queue, &is_empty);

    ZV_LOG_DEBUG(
        _zv_channel_log,
        "0x%" PRIxPTR ": stop_task: out_queue: close_rc=%d, is_empty=%d",
        channel, rc, is_empty
    );

    if (rc < 0)
        ; // already closed, do nothing
    else
    {
        if (is_empty)
            zv_channel_release(channel);
        else
        {
            // outbound queue was not empty, 'close' flag is set
            // channel will be released by the writer task
            // when queue will be empty
        }
    }

    // channel has no running read operation now
    int is_stopped;
    is_empty = zv_channel_in_queue_close(&channel->in_queue, &is_stopped);
    assert(is_stopped);

    ZV_LOG_DEBUG(
        _zv_channel_log,
        "0x%" PRIxPTR ": read_task: in_queue: is_empty=%d is_stopped=%d",
        channel, is_empty, is_stopped
    );

    if (is_empty)
    {
        channel->close_handler(channel);
        zv_channel_release(channel);
    }

    zv_channel_release(channel);
}


int zv_channel_stop(zv_channel_t * channel)
{
    int rc = zv_channel_in_queue_stop(&channel->in_queue);
    ZV_LOG_DEBUG(_zv_channel_log, "0x%" PRIxPTR ": stop: stop_rc=%d", channel, rc);
    if (rc != 0)
        return -1;

    struct zv_channel_stop_read_s * stop_read = malloc(sizeof(struct zv_channel_stop_read_s));
    if (!stop_read)
        return -1;

    stop_read->channel = channel;

    _zv_event_handler_init(
        &stop_read->handler,
        channel->read_handler.sockfd,
        zv_event_type_read,
        zv_event_opcode_remove,
        &_zv_channel_stop_task
    );

    _zv_loop_add_handler(channel->loop, &stop_read->handler);
    return 0;
}


zv_channel_t * zv_channel_retain(zv_channel_t * channel)
{
    atomic_fetch_add(&channel->retain_count, 1);
    return channel;
}


void zv_channel_release(zv_channel_t * channel)
{
    size_t retain_count = atomic_fetch_sub(&channel->retain_count, 1) - 1;
    if (retain_count == 0)
    {
    #if defined(_ZV_HAS_RDTSC)
        ZV_LOG_DEBUG(
            _zv_channel_log,
            "0x%" PRIxPTR ": deinit, total_reads=%zu, async_reads=%zu",
            (intptr_t) channel,
            channel->read_stats.total_reads,
            channel->read_stats.async_reads
        );
    #else
        ZV_LOG_DEBUG(_zv_channel_log, "0x%" PRIxPTR ": deinit", (intptr_t) channel);
    #endif
        zv_socket_t sockfd = channel->read_handler.sockfd;
        zv_socket_close(sockfd);
        zv_channel_out_queue_deinit(&channel->out_queue);
        zv_channel_in_queue_deinit(&channel->in_queue);
        zv_buffer_cache_release(channel->in_queue.buffer_cache);
        free(channel->peer_addr_str);
        free(channel->local_addr_str);
        free(channel);
    }
}


zv_loop_t * zv_channel_get_loop(zv_channel_t * channel)
{
    return channel->loop;
}


void * zv_channel_set_extra_data(zv_channel_t * channel, void * extra_data)
{
    void * ret = channel->extra_data;
    channel->extra_data = extra_data;
    return ret;
}


void * zv_channel_get_extra_data(zv_channel_t * channel)
{
    return channel->extra_data;
}


const struct sockaddr * zv_channel_get_local_addr(zv_channel_t * channel, socklen_t * addrlen)
{
    size_t channel_size = _zv_round(sizeof(struct zv_channel_s), sizeof(void*));
    *addrlen = channel->local_addr_len;
    return (struct sockaddr*) (((char*)channel) + channel_size);
}


const char * zv_channel_get_local_addr_str(zv_channel_t * channel)
{
    return channel->local_addr_str;
}


const struct sockaddr * zv_channel_get_peer_addr(zv_channel_t * channel, socklen_t * addrlen)
{
    size_t channel_size = _zv_round(sizeof(struct zv_channel_s), sizeof(void*));
    size_t local_addr_size = _zv_round(channel->local_addr_len, sizeof(void*));
    *addrlen = channel->peer_addr_len;
    return (struct sockaddr*) (((char*)channel) + (channel_size + local_addr_size));
}


const char * zv_channel_get_peer_addr_str(zv_channel_t * channel)
{
    return channel->peer_addr_str;
}


int zv_channel_send(zv_channel_t * channel, const void * data, size_t size)
{
    int rc = zv_channel_out_queue_add_data(&channel->out_queue, data, size);
    if (rc > 0)
    {
        // should start writting socket
        zv_thread_pool_dispatch(channel->thread_pool, &channel->write_handler.task);
        return 0;
    }
    else
    {
        // rc is either
        // 0: data added, but there is a thread writting socket right now
        // -1: channel is closed
        return rc;
    }
}


int zv_channel_send_sync(zv_channel_t * channel, const void * data, size_t size)
{
    // We could write data directly to the socket here without
    // copying it to the out queue, and it would work well in 99% of cases.
    // However, handling socket write errors would be painful.
    // Perhaps later...
    int rc = zv_channel_out_queue_add_data(&channel->out_queue, data, size);
    if (rc > 0)
    {
        _zv_channel_write_task(&channel->write_handler.task);
        return 0;
    }
    else
    {
        // rc is either
        // 0: data added, but there is a thread writting socket right now
        // -1: channel is closed
        return rc;
    }
}


#if defined(ZV_CHANNEL_SEND_REF)

int zv_channel_send_ref(
    zv_channel_t * channel,
    const void * data,
    size_t size,
    void (*after_send)(void*),
    void * thunk
) {
    int rc = zv_channel_out_queue_add_ref(&channel->out_queue, data, size, after_send, thunk);
    if (rc > 0)
    {
        // should start writting socket
        zv_thread_pool_dispatch(channel->thread_pool, &channel->write_handler.task);
        return 0;
    }
    else
    {
        // rc is either
        // 0: data added, but there is a thread writting socket right now
        // -1: channel is closed
        return rc;
    }
}

#endif


void zv_channel_set_log_level(enum zv_log_level level)
{
    _zv_channel_log.level = level;
}
