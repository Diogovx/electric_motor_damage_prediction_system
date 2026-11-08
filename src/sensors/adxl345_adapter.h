#pragma once
#include "i_sensor.h"
#include "driver/i2c_master.h"

class ADXL345Adapter : public ISensor {
public:
    bool init() override;
    void readData(float* buffer) override;      // 3 floats: ax, ay, az em m/s2
    uint32_t getSamplingRate() override;
    void setRange(uint8_t range);               // 2, 4, 8 ou 16 (g)
private:
    uint8_t i2cAddress = 0x53;
    uint8_t rangeG = 16;
    i2c_master_dev_handle_t dev = nullptr;      // detalhe de implementacao
};