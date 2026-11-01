#include "acquisition.h"

#include "../config/system_config.h"

#include "../sensors/ina219.h"
#include "../sensors/adxl345.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void acquisitionTask(void* parameter)
{
    const auto& config = getSensorConfiguration();

    while (true)
    {
        if (config.ina219Enabled)
        {
            INA219Data data;

            if (ina219Read(data))
            {
                // Futuramente:
                // enviar para buffer
            }
        }

        if (config.adxl345Enabled)
        {
            ADXL345Data data;

            if (adxl345Read(data))
            {
                // Futuramente:
                // enviar para buffer
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void acquisitionInit()
{
    const auto& config = getSensorConfiguration();

    if (config.ina219Enabled)
    {
        ina219Init();
    }

    if (config.adxl345Enabled)
    {
        adxl345Init();
    }
}

void acquisitionStart()
{
    xTaskCreate(
        acquisitionTask,
        "acquisition_task",
        4096,
        nullptr,
        5,
        nullptr
    );
}