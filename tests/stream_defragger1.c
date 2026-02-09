#include "zv/stream_defragger.h"
#include <stdint.h>

static size_t message_size_func(const void * data)
{
    const uint8_t * ptr = (const uint8_t*) data;
    uint32_t message_size = 0;
    for (int c=4; c>0; c--, ptr++)
        message_size |= *ptr;
    return message_size;
};

int stream_defragger1()
{
    struct zv_stream_defragger_s stream_defragger;
    zv_stream_defragger_init(&stream_defragger);

    const uint8_t data_block_1[] = { 0x00, 0x00 };
    const uint8_t data_block_2[] = { 0x00 };
    const uint8_t data_block_3[] = { 0x07, 0x00, 0x42, 0x42, /*next message*/ 0x00, 0x00 };
    const uint8_t data_block_4[] = { 0x00, 0x09, 0x01, 0x42 };
    const uint8_t data_block_5[] = { 0x42, 0x42, 0x42 };

    struct data_block_s
    {
        size_t size;
        const uint8_t * data;
    }
    const data_blocks[] =
    {
        { sizeof(data_block_1), data_block_1 },
        { sizeof(data_block_2), data_block_2 },
        { sizeof(data_block_3), data_block_3 },
        { sizeof(data_block_4), data_block_4 },
        { sizeof(data_block_5), data_block_5 },
        {0}
    };

    const size_t header_size = sizeof(uint32_t);
    const struct data_block_s * data_block = data_blocks;
    size_t message_count = 0;
    for (; data_block->size > 0; data_block++)
    {
        struct zv_stream_defragger_state_s state = { data_block->data, data_block->size };
        const void * data = (const void*) data_block->data;
        size_t data_size = data_block->size;
        for (;;)
        {
            size_t message_size;
            const void * message = zv_stream_defragger_get_next(
                &stream_defragger,
                header_size,
                &message_size_func,
                &state,
                &message_size
            );

            if (!message)
                break;

            const char * ptr = (((const char*)message) + header_size);
            if (*ptr != message_count)
                break;

            message_count++;
        }
    }

    zv_stream_defragger_deinit(&stream_defragger);

    return (message_count == 2) ? 0 : -1;
}
