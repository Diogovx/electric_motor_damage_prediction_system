#include "ina219_adapter.h"

#include "../drivers/i2c_bus.h"

#include "esp_log.h"

#include <math.h>

namespace
{
    constexpr char TAG[] = "INA219";

    constexpr uint8_t REG_CONFIG = 0x00;
    constexpr uint8_t REG_SHUNT  = 0x01;

    constexpr float SHUNT_OHMS = 0.1f;          // resistor do modulo
    constexpr uint32_t SAMPLING_RATE_HZ = 100;
    constexpr int TIMEOUT_MS = 20;

    // 32 V | ganho (bits 12:11) | ADC bus 12 bits | ADC shunt 12 bits | continuo
    // Com ganho 3 resulta em 0x399F, igual a coleta (setCalibration_32V_2A)
    uint16_t buildConfig(uint8_t gain)
    {
        return 0x2000 | ((gain & 0x03) << 11) | 0x0180 | 0x0018 | 0x0007;
    }

    bool writeReg16(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t value)
    {
        const uint8_t buf[3] = {reg,
                                static_cast<uint8_t>(value >> 8),
                                static_cast<uint8_t>(value & 0xFF)};
        return i2c_master_transmit(dev, buf, sizeof(buf), TIMEOUT_MS) == ESP_OK;
    }

    bool readReg16(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t& value)
    {
        uint8_t raw[2];
        if (i2c_master_transmit_receive(dev, &reg, 1, raw, 2, TIMEOUT_MS) != ESP_OK)
        {
            return false;
        }
        value = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
        return true;
    }
}

bool INA219Adapter::writeConfig()
{
    return dev != nullptr && writeReg16(dev, REG_CONFIG, buildConfig(currentGain));
}

bool INA219Adapter::init()
{
    if (dev != nullptr)
    {
        return true;
    }

    i2c_master_bus_handle_t bus = i2cBusGetHandle();
    if (bus == nullptr)
    {
        ESP_LOGE(TAG, "Barramento I2C nao inicializado");
        return false;
    }

    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = i2cAddress;
    cfg.scl_speed_hz = 100000;

    if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao registrar dispositivo no barramento");
        dev = nullptr;
        return false;
    }

    if (!writeConfig())
    {
        ESP_LOGE(TAG, "Sem resposta (fiacao, alimentacao ou endereco 0x%02X)", i2cAddress);
        i2c_master_bus_rm_device(dev);
        dev = nullptr;
        return false;
    }

    uint16_t config = 0;
    if (!readReg16(dev, REG_CONFIG, config))
    {
        ESP_LOGW(TAG, "Nao consegui reler a configuracao");
    }

    ESP_LOGI(TAG, "OK (endereco 0x%02X, config 0x%04X, shunt %.3f ohm)",
             i2cAddress, config, SHUNT_OHMS);
    return true;
}

void INA219Adapter::calibrate()
{
    if (!writeConfig())
    {
        ESP_LOGE(TAG, "Falha ao aplicar a configuracao");
    }
}

void INA219Adapter::readData(float* buffer)
{
    uint16_t raw = 0;

    if (dev == nullptr || !readReg16(dev, REG_SHUNT, raw))
    {
        buffer[0] = NAN;
        return;
    }

    const float shunt_mV = static_cast<int16_t>(raw) * 0.01f;   // 10 uV/LSB
    buffer[0] = shunt_mV / SHUNT_OHMS;                          // mV / ohm = mA
}

uint32_t INA219Adapter::getSamplingRate()
{
    return SAMPLING_RATE_HZ;
}