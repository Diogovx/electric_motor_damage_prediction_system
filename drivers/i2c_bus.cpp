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

