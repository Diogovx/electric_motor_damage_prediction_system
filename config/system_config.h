#pragma once

struct SensorConfiguration
{
    bool ina219Enabled;
    bool adxl345Enabled;
};

void initSystemConfiguration();

const SensorConfiguration& getSensorConfiguration();