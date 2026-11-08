#pragma once
#include "i_sensor.h"
#include "driver/i2c_master.h"

class INA219Adapter : public ISensor {
public:
    bool init() override;
    void readData(float* buffer) override;      // 1 float: corrente em mA
    uint32_t getSamplingRate() override;
    void calibrate();
private:
    bool writeConfig();                         // detalhe de implementacao
    uint8_t i2cAddress = 0x40;
    uint8_t currentGain = 3;                    // PGA /8 (+-320 mV), como na coleta
    i2c_master_dev_handle_t dev = nullptr;
};