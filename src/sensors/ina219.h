#pragma once

#include <stdint.h>

struct INA219Data
{
    uint64_t timestamp;

    float busVoltage;
    float shuntVoltage;
    float current;
    float power;
};

bool ina219Init();

bool ina219Read(INA219Data& data);