#include "ina219.h"

#include "../drivers/i2c_bus.h"

#include "esp_log.h"
#include "esp_timer.h"

namespace
{
    constexpr char TAG[] = "INA219";

    constexpr uint8_t ADDR = 0x40;

    constexpr uint8_t REG_CONFIG  = 0x00;
    constexpr uint8_t REG_SHUNT   = 0x01;
    constexpr uint8_t REG_BUS     = 0x02;

    // Resistor shunt do modulo (a maioria dos modulos usa 0,1 ohm)
    constexpr float SHUNT_OHMS = 0.1f;
    constexpr int TIMEOUT_MS = 20;

    i2c_master_dev_handle_t dev = nullptr;

    bool writeReg16(uint8_t reg, uint16_t value)
    {
        const uint8_t buf[3] = {reg,
                                static_cast<uint8_t>(value >> 8),
                                static_cast<uint8_t>(value & 0xFF)};
        return i2c_master_transmit(dev, buf, sizeof(buf), TIMEOUT_MS) == ESP_OK;
    }

    bool readReg16(uint8_t reg, uint16_t& value)
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

bool ina219Init()
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
    cfg.device_address = ADDR;
    cfg.scl_speed_hz = 100000;

    if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao registrar dispositivo no barramento");
        dev = nullptr;
        return false;
    }

    // 32 V, ganho /8 (+-320 mV), 12 bits, continuo
    if (!writeReg16(REG_CONFIG, 0x399F))
    {
        ESP_LOGE(TAG, "Sem resposta em 0x%02X (fiacao, alimentacao, endereco?)", ADDR);
        return false;
    }

    uint16_t config = 0;
    if (!readReg16(REG_CONFIG, config))
    {
        ESP_LOGE(TAG, "Falha ao reler a configuracao");
        return false;
    }

    ESP_LOGI(TAG, "OK (config 0x%04X, shunt %.3f ohm)", config, SHUNT_OHMS);
    return true;
}

bool ina219Read(INA219Data& data)
{
    uint16_t rawShunt = 0;
    uint16_t rawBus = 0;

    if (dev == nullptr || !readReg16(REG_SHUNT, rawShunt) || !readReg16(REG_BUS, rawBus))
    {
        return false;
    }

    const float shunt_mV = static_cast<int16_t>(rawShunt) * 0.01f;  // 10 uV/LSB
    const float bus_V = (rawBus >> 3) * 0.004f;                      // 4 mV/LSB

    data.timestamp = esp_timer_get_time();
    data.shuntVoltage = shunt_mV;
    data.busVoltage = bus_V;
    data.current = shunt_mV / SHUNT_OHMS;          // mV / ohm = mA
    data.power = bus_V * data.current;             // mW

    return true;
}

