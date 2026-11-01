#pragma once

#include <stdint.h>

struct ADXL345Data
{
    uint64_t timestamp;

    float x;
    float y;
    float z;
};

bool adxl345Init();

bool adxl345Read(ADXL345Data& data);