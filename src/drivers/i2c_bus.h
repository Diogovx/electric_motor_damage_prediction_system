#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t i2cBusInit();
i2c_master_bus_handle_t i2cBusGetHandle();
bool i2cBusProbe(uint8_t address);