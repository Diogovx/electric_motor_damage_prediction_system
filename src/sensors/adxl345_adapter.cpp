#include "adxl345_adapter.h"

#include "../drivers/i2c_bus.h"

#include "esp_log.h"

#include <math.h>

namespace
{
    constexpr char TAG[] = "ADXL345";

    constexpr uint8_t REG_DEVID       = 0x00;
    constexpr uint8_t REG_BW_RATE     = 0x2C;
    constexpr uint8_t REG_POWER_CTL   = 0x2D;
    constexpr uint8_t REG_DATA_FORMAT = 0x31;
    constexpr uint8_t REG_DATAX0      = 0x32;

    constexpr uint8_t DEVID_EXPECTED = 0xE5;
    constexpr uint8_t FULL_RES_BIT   = 0x08;

    // 4 mg/LSB (valor da biblioteca Adafruit usada na coleta) x 9,80665 = m/s2
    constexpr float MS2_PER_LSB = 0.004f * 9.80665f;

    constexpr uint32_t SAMPLING_RATE_HZ = 100;
    constexpr int TIMEOUT_MS = 20;

    bool writeReg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
    {
        const uint8_t buf[2] = {reg, value};
        return i2c_master_transmit(dev, buf, sizeof(buf), TIMEOUT_MS) == ESP_OK;
    }

    bool readRegs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* out, size_t len)
    {
        return i2c_master_transmit_receive(dev, &reg, 1, out, len, TIMEOUT_MS) == ESP_OK;
    }

    bool rangeToBits(uint8_t rangeG, uint8_t& bits)
    {
        switch (rangeG)
        {
            case 2:  bits = 0; return true;
            case 4:  bits = 1; return true;
            case 8:  bits = 2; return true;
            case 16: bits = 3; return true;
            default: return false;
        }
    }
}

bool ADXL345Adapter::init()
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

    // Se algo falhar depois de registrar, desfaz o registro para um novo init() funcionar
    auto fail = [this](const char* msg)
    {
        ESP_LOGE(TAG, "%s", msg);
        i2c_master_bus_rm_device(dev);
        dev = nullptr;
        return false;
    };

    uint8_t id = 0;
    if (!readRegs(dev, REG_DEVID, &id, 1))
    {
        return fail("Sem resposta (fiacao, alimentacao ou endereco)");
    }
    if (id != DEVID_EXPECTED)
    {
        return fail("DEVID inesperado (esperado 0xE5)");
    }

    uint8_t bits = 0;
    if (!rangeToBits(rangeG, bits))
    {
        return fail("Faixa invalida");
    }

    bool ok = true;
    ok &= writeReg(dev, REG_BW_RATE, 0x0A);                  // 100 Hz
    ok &= writeReg(dev, REG_DATA_FORMAT, FULL_RES_BIT | bits);
    ok &= writeReg(dev, REG_POWER_CTL, 0x08);                // modo medicao
    if (!ok)
    {
        return fail("Falha ao configurar registradores");
    }

    ESP_LOGI(TAG, "OK (endereco 0x%02X, %lu Hz, +-%d g)",
             i2cAddress, (unsigned long)SAMPLING_RATE_HZ, (int)rangeG);
    return true;
}

void ADXL345Adapter::readData(float* buffer)
{
    uint8_t raw[6];

    if (dev == nullptr || !readRegs(dev, REG_DATAX0, raw, sizeof(raw)))
    {
        buffer[0] = buffer[1] = buffer[2] = NAN;
        return;
    }

    const int16_t x = static_cast<int16_t>((raw[1] << 8) | raw[0]);
    const int16_t y = static_cast<int16_t>((raw[3] << 8) | raw[2]);
    const int16_t z = static_cast<int16_t>((raw[5] << 8) | raw[4]);

    buffer[0] = x * MS2_PER_LSB;
    buffer[1] = y * MS2_PER_LSB;
    buffer[2] = z * MS2_PER_LSB;
}

uint32_t ADXL345Adapter::getSamplingRate()
{
    return SAMPLING_RATE_HZ;
}

void ADXL345Adapter::setRange(uint8_t range)
{
    uint8_t bits = 0;
    if (!rangeToBits(range, bits))
    {
        ESP_LOGW(TAG, "Faixa %d g invalida (use 2, 4, 8 ou 16)", (int)range);
        return;
    }

    rangeG = range;

    if (dev != nullptr && !writeReg(dev, REG_DATA_FORMAT, FULL_RES_BIT | bits))
    {
        ESP_LOGE(TAG, "Falha ao gravar a nova faixa");
    }
}