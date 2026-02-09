#if !defined(ZV_C10K_H)
#define ZV_C10K_H

#include <stdint.h>

#define PING_INTERVAL_SEC 2

#define MSG_PING 1
#define MSG_PONG 2

static inline void encode_uint16(void * dst, uint16_t value)
{
    uint8_t * ptr = dst;
    ptr[0] = (value >> 8);
    ptr[1] = (value & 0xFF);
}

static inline uint16_t decode_uint16(const void * src)
{
    const uint8_t * ptr = src;
    return (((uint16_t)ptr[0]) << 8) | ptr[1];
}

#endif /* !defined(ZV_C10K_H) */
