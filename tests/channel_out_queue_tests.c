
#include "../include/zv/buffer.h"
#include "../include/zv/log.h"
#include "../src/channel_out_queue.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <Winsock2.h>
#define iovec _WSABUF
#define IOV_MAX 64
static inline size_t iovec_get_len(const struct iovec * iov) { return iov->len; }
static inline void * iovec_get_base(const struct iovec * iov) { return iov->buf; }
#else
#include <sys/uio.h>
#include <limits.h>
static inline size_t iovec_get_len(const struct iovec * iov) { return iov->iov_len; }
static inline void * iovec_get_base(const struct iovec * iov) { return iov->iov_base; }
#endif

#define ARRAY_SIZE(__array) (sizeof((__array))/sizeof((__array)[0]))

#define EXPECT_EQ(__v1, __v2) \
    do \
    { \
        if ((__v1) != (__v2)) \
        { \
            fprintf(stderr, #__v1 " != " #__v2 " (%s:%d)\n", __FILE__, __LINE__); \
            return -1; \
        } \
    } \
    while (0)

#define EXPECT_IOV(__iov, __expected_sizes) \
    do \
    { \
        if (check_iov(ARRAY_SIZE((__expected_sizes)), (__iov), (__expected_sizes), __FILE__, __LINE__) != 0) \
            return -1; \
    } while (0)


static void prepare_message(char * message, uint16_t size)
{
    memcpy(message, &size, sizeof(size));
    for (size_t idx=2; idx<size; idx++)
        message[idx] = (char)idx;
}


static int check_iov(
    size_t iovc,
    const struct iovec * iov,
    const size_t * expected_size,
    const char * file,
    int line
) {
    for (size_t idx=0; idx<iovc; idx++)
    {
        size_t iov_len = iovec_get_len(iov);
        if (iov_len != *expected_size)
        {
            fprintf(
                stderr,
                "[%zu].len is %zu instead of %zu (%s:%d)",
                idx,
                iov_len,
                *expected_size,
                file,
                line
            );
            return -1;
        }
        iov++;
        expected_size++;
    }
    return 0;
}


static int test_remove1(size_t buffer_size, struct zv_channel_out_queue_s * out_queue)
{
    EXPECT_EQ(buffer_size, 100);
    //    buffer1     buffer2     buffer3     buffer4
    // |-----------|-----------|-----------|-----------|
    //  <---------><--------------------------------->
    //   message1             message2
    //
    struct iovec iov[IOV_MAX];
    char message1[96];
    char message2[99+100+95];
    size_t blocks_remaining;
    int iovc;

    prepare_message(message1, sizeof(message1));
    prepare_message(message2, sizeof(message2));

    zv_channel_out_queue_add_data(out_queue, message1, sizeof(message1));
    zv_channel_out_queue_add_data(out_queue, message2, sizeof(message2));

    iovc  = zv_channel_out_queue_get_data(out_queue, iov, IOV_MAX);
    size_t expected_sizes_1[4] = { 96, 99, 100, 95 };
    EXPECT_EQ(iovc, ARRAY_SIZE(expected_sizes_1));
    EXPECT_IOV(iov, expected_sizes_1);

    // now remove 96+99+1 bytes, so the remaining of the message2
    // is greater than 128, what will require 2 bytes in the ULEB128 encoding,
    // and those bytes will be on the edge of buffer2 and buffer3
    //
    //    buffer1     buffer2     buffer3     buffer4
    // |-----------|-----------|-----------|-----------|
    //                         <--------------------->
    //                         remaining of the message2
    //
    blocks_remaining = zv_channel_out_queue_remove(out_queue, 96+99+1);
    EXPECT_EQ(blocks_remaining, 1);

    iovc = zv_channel_out_queue_get_data(out_queue, iov, IOV_MAX);
    size_t expected_sizes_2[2] = { 99, 95 };
    EXPECT_EQ(iovc, ARRAY_SIZE(expected_sizes_2));
    EXPECT_IOV(iov, expected_sizes_2);

    blocks_remaining = zv_channel_out_queue_remove(out_queue, expected_sizes_2[0] + expected_sizes_2[1]);
    EXPECT_EQ(blocks_remaining, 0);

    return 0;
}


static int test_remove2(size_t buffer_size, struct zv_channel_out_queue_s * out_queue)
{
    EXPECT_EQ(buffer_size, 100);

    // First message completely occupies two buffers
    //   buffer1   buffer2   buffer3
    // |---------|---------|---------|
    //  <-----------------> <------>
    //       message1        message2
    // 
    char message1[197];
    for (size_t idx=0; idx<sizeof(message1); idx++)
        message1[idx] = (char)idx;

    char message2[42];
    for (size_t idx=0; idx<sizeof(message2); idx++)
        message2[idx] = (char)idx;

    zv_channel_out_queue_add_data(out_queue, message1, sizeof(message1));
    zv_channel_out_queue_add_data(out_queue, message2, sizeof(message2));

    struct iovec iov[IOV_MAX];
    int iovc = zv_channel_out_queue_get_data(out_queue, iov, IOV_MAX);
    size_t expected_sizes[3] = { 97, 100, 42 };
    EXPECT_EQ(iovc, ARRAY_SIZE(expected_sizes));
    EXPECT_IOV(iov, expected_sizes);

    size_t blocks_remaining = zv_channel_out_queue_remove(out_queue, sizeof(message1) + sizeof(message2));
    EXPECT_EQ(blocks_remaining, 0);

    return 0;
}


static void after_send(void * thunk)
{
    (*((int*)thunk))++;
}

static int test_cb(size_t buffer_size, struct zv_channel_out_queue_s * out_queue)
{
    EXPECT_EQ(buffer_size, 50);

    char message[14];
    for (size_t idx = 0; idx<sizeof(message); idx++)
        message[idx] = (char)idx;

    int after_send_called_times = 0;

    zv_channel_out_queue_add_ref(
        out_queue,
        message,
        sizeof(message),
        &after_send,
        &after_send_called_times
    );

    zv_channel_out_queue_add_ref(
        out_queue,
        message,
        sizeof(message),
        &after_send,
        &after_send_called_times
    );

    size_t blocks_remaining;
    struct iovec iov[IOV_MAX];
    int iovc;

    iovc = zv_channel_out_queue_get_data(out_queue, iov, IOV_MAX);
    size_t expected_sizes_1[2] = { 14, 14 };
    EXPECT_EQ(iovc, ARRAY_SIZE(expected_sizes_1));
    EXPECT_IOV(iov, expected_sizes_1);

    blocks_remaining = zv_channel_out_queue_remove(out_queue, sizeof(message) + sizeof(message)/2);
    EXPECT_EQ(blocks_remaining, 1);
    EXPECT_EQ(after_send_called_times, 1);

    iovc = zv_channel_out_queue_get_data(out_queue, iov, IOV_MAX);
    size_t expected_sizes_2[1] = { 7 };
    EXPECT_EQ(iovc, ARRAY_SIZE(expected_sizes_2));
    EXPECT_IOV(iov, expected_sizes_2);

    after_send_called_times = 0;
    blocks_remaining = zv_channel_out_queue_remove(out_queue, sizeof(message)/2);
    EXPECT_EQ(blocks_remaining, 0);
    EXPECT_EQ(after_send_called_times, 1);

    return 0;
}


int run_test(
    size_t buffer_size,
    int (*test_func)(size_t, struct zv_channel_out_queue_s*)
) {
    zv_buffer_cache_t * buffer_cache = zv_buffer_cache_create(buffer_size, 16, 0);
    if (buffer_cache == NULL)
    {
        fprintf(stderr, "zv_buffer_cache_create() failed\n");
        return -1;
    }

    struct zv_channel_out_queue_s out_queue;
    zv_channel_out_queue_init(&out_queue, buffer_cache);

    int rc = test_func(buffer_size, &out_queue);

    zv_channel_out_queue_deinit(&out_queue);
    zv_buffer_cache_release(buffer_cache);
    return rc;
}


#define RUN_TEST(buffer_size, name) \
    do { \
        int rc = run_test((buffer_size), &(name)); \
        if (rc != 0) \
            fprintf(stderr, "test %s failed", #name); \
    } while (0)

int main(int argc, const char * argv[])
{
    /*
    FILE * file = fopen("channel_out_queue_tests.log", "w");
    zv_log_backend_t * log_backend = zv_log_add_file_backend(file);
    zv_buffer_cache_set_log_level(zv_log_level_debug);
    */

    RUN_TEST(100, test_remove1);
    RUN_TEST(100, test_remove2);
    RUN_TEST(50, test_cb);

    /*
    if (log_backend)
    {
        FILE * file = zv_log_remove_file_backend(log_backend);
        if (file)
            fclose(file);
    }
    */

    return 0;
}

#undef RUN_TEST
