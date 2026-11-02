#include "system_config.h"

static SensorConfiguration config;

void initSystemConfiguration()
{
    config.ina219Enabled = true;
    config.adxl345Enabled = true;
}

const SensorConfiguration& getSensorConfiguration()
{
    return config;
}
