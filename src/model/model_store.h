#pragma once

#include <stdint.h>

struct ModelInfo
{
    const uint8_t* data;
    uint32_t size;
    uint32_t version;
    uint32_t crc32;
};
bool modelStoreOpen(uint8_t slot, ModelInfo& info);