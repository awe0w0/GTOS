#ifndef GTOS_COMMON_CRC32_H
#define GTOS_COMMON_CRC32_H

#include <common/types.h>

namespace gtos { namespace common {
    inline uint32_t Crc32Byte(uint32_t state, uint8_t byte) {
        state ^= byte;
        for (uint32_t bit = 0; bit < 8; ++bit)
            state = (state >> 1) ^ ((state & 1) ? 0xEDB88320u : 0);
        return state;
    }

    inline uint32_t Crc32Update(uint32_t state, const uint8_t* data, uint32_t bytes) {
        for (uint32_t i = 0; i < bytes; ++i)
            state = Crc32Byte(state, data[i]);
        return state;
    }
} }

#endif
