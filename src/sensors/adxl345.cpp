#include "adxl345.h"

#include "../drivers/i2c_bus.h"

#include "esp_log.h"
#include "esp_timer.h"

namespace
{
    constexpr char TAG[] = "ADXL345";

    constexpr uint8_t ADDR = 0x53;  // SDO em GND (use 0x1D se SDO em 3V3)

    constexpr uint8_t REG_DEVID       = 0x00;
    constexpr uint8_t REG_BW_RATE     = 0x2C;
    constexpr uint8_t REG_POWER_CTL   = 0x2D;
    constexpr uint8_t REG_DATA_FORMAT = 0x31;
    constexpr uint8_t REG_DATAX0      = 0x32;

    constexpr float G_PER_LSB = 0.0039f;  // full resolution: 3,9 mg/LSB
    constexpr int TIMEOUT_MS = 20;

    i2c_master_dev_handle_t dev = nullptr;

    bool writeReg(uint8_t reg, uint8_t value)
    {
        const uint8_t buf[2] = {reg, value};
        return i2c_master_transmit(dev, buf, sizeof(buf), TIMEOUT_MS) == ESP_OK;
    }

    bool readRegs(uint8_t reg, uint8_t* out, size_t len)
    {
        return i2c_master_transmit_receive(dev, &reg, 1, out, len, TIMEOUT_MS) == ESP_OK;
    }
}

bool adxl345Init()
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

    uint8_t id = 0;
    if (!readRegs(REG_DEVID, &id, 1))
    {
        ESP_LOGE(TAG, "Sem resposta em 0x%02X (fiacao, alimentacao, endereco?)", ADDR);
        return false;
    }

    if (id != 0xE5)
    {
        ESP_LOGE(TAG, "DEVID inesperado: 0x%02X (esperado 0xE5)", id);
        return false;
    }

    bool ok = true;
    ok &= writeReg(REG_BW_RATE, 0x0A);      // 100 Hz
    ok &= writeReg(REG_DATA_FORMAT, 0x08);  // full resolution, +-2 g
    ok &= writeReg(REG_POWER_CTL, 0x08);    // modo medicao

    if (!ok)
    {
        ESP_LOGE(TAG, "Falha ao configurar registradores");
        return false;
    }

    ESP_LOGI(TAG, "OK (DEVID 0xE5, 100 Hz, +-2 g)");
    return true;
}

bool adxl345Read(ADXL345Data& data)
{
    uint8_t raw[6];

    if (dev == nullptr || !readRegs(REG_DATAX0, raw, sizeof(raw)))
    {
        return false;
    }

    const int16_t x = static_cast<int16_t>((raw[1] << 8) | raw[0]);
    const int16_t y = static_cast<int16_t>((raw[3] << 8) | raw[2]);
    const int16_t z = static_cast<int16_t>((raw[5] << 8) | raw[4]);

    data.timestamp = esp_timer_get_time();
    data.x = x * G_PER_LSB;
    data.y = y * G_PER_LSB;
    data.z = z * G_PER_LSB;

    return true;
}