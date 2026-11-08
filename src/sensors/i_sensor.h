#pragma once
#include <stdint.h>

class ISensor {
public:
    virtual ~ISensor() = default;
    virtual bool init() = 0;
    virtual void readData(float* buffer) = 0;   // falha => preenche com NaN
    virtual uint32_t getSamplingRate() = 0;     // Hz
};