#include "i2c_bus.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

namespace
{
    constexpr gpio_num_t I2C_SDA_GPIO = GPIO_NUM_8;
    constexpr gpio_num_t I2C_SCL_GPIO = GPIO_NUM_9;

    constexpr i2c_port_num_t I2C_PORT = I2C_NUM_0;

    constexpr char TAG[] = "I2C_BUS";

    i2c_master_bus_handle_t busHandle = nullptr;
}

esp_err_t i2cBusInit()
{
    if (busHandle != nullptr)
    {
        ESP_LOGW(TAG, "Barramento I2C ja inicializado");
        return ESP_OK;
    }

    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
            .allow_pd = false
        }
    };

    const esp_err_t err = i2c_new_master_bus(&config, &busHandle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao inicializar I2C: %s",
                 esp_err_to_name(err));

        return err;
    }

    ESP_LOGI(TAG, "Barramento I2C inicializado");
    ESP_LOGI(TAG, "SDA: GPIO %d", static_cast<int>(I2C_SDA_GPIO));
    ESP_LOGI(TAG, "SCL: GPIO %d", static_cast<int>(I2C_SCL_GPIO));

    return ESP_OK;
}

i2c_master_bus_handle_t i2cBusGetHandle()
{
    return busHandle;
}

bool i2cBusProbe(uint8_t address)
{
    if (busHandle == nullptr)
    {
        return false;
    }

    return i2c_master_probe(busHandle, address, 50) == ESP_OK;
}

void i2cBusScan()
{
    if (busHandle == nullptr)
    {
        return;
    }

    ESP_LOGI(TAG, "Varredura I2C (0x08..0x77):");

    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++)
    {
        if (i2c_master_probe(busHandle, addr, 20) == ESP_OK)
        {
            ESP_LOGI(TAG, "  dispositivo em 0x%02X", addr);
            found++;
        }
    }

    ESP_LOGI(TAG, "%d dispositivo(s) encontrado(s)", found);
}