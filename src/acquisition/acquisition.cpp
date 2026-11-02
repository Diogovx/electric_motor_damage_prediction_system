#include "acquisition.h"

#include "../config/system_config.h"
#include "../sensors/ina219.h"
#include "../sensors/adxl345.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ACQ";

static constexpr TickType_t SAMPLE_PERIOD_TICKS = pdMS_TO_TICKS(10);  // 100 Hz
static constexpr int64_t REPORT_INTERVAL_US = 1000000;                // 1 s

static void acquisitionTask(void* parameter)
{
    const auto& config = getSensorConfiguration();

    INA219Data ina{};
    ADXL345Data adxl{};

    uint32_t samples = 0;
    uint32_t inaErrors = 0;
    uint32_t adxlErrors = 0;
    uint32_t overruns = 0;

    TickType_t lastWake = xTaskGetTickCount();
    int64_t lastReport = esp_timer_get_time();

    ESP_LOGI(TAG, "Tarefa de aquisicao em execucao (nucleo %d)", xPortGetCoreID());

    while (true)
    {
        // pdFALSE = perdeu o prazo (o ciclo anterior demorou mais que 10 ms)
        if (xTaskDelayUntil(&lastWake, SAMPLE_PERIOD_TICKS) == pdFALSE)
        {
            overruns++;
        }

        if (config.ina219Enabled && !ina219Read(ina))
        {
            inaErrors++;
        }

        if (config.adxl345Enabled && !adxl345Read(adxl))
        {
            adxlErrors++;
        }

        samples++;

        // Futuramente: enviar ina/adxl para o buffer circular

        const int64_t now = esp_timer_get_time();
        if (now - lastReport >= REPORT_INTERVAL_US)
        {
            ESP_LOGI(TAG,
                     "%lu amostras/s | erros INA=%lu ADXL=%lu | atrasos=%lu",
                     (unsigned long)samples,
                     (unsigned long)inaErrors,
                     (unsigned long)adxlErrors,
                     (unsigned long)overruns);

            ESP_LOGI(TAG,
                     "ultima: I=%.1f mA | a=(%.3f, %.3f, %.3f)",
                     ina.current, adxl.x, adxl.y, adxl.z);

            samples = 0;
            inaErrors = 0;
            adxlErrors = 0;
            overruns = 0;
            lastReport = now;
        }
    }
}

bool acquisitionInit()
{
    const auto& config = getSensorConfiguration();
    bool ok = true;

    if (config.ina219Enabled)
    {
        if (ina219Init())
        {
            ESP_LOGI(TAG, "INA219 inicializado");
        }
        else
        {
            ESP_LOGE(TAG, "Falha ao inicializar INA219");
            ok = false;
        }
    }

    if (config.adxl345Enabled)
    {
        if (adxl345Init())
        {
            ESP_LOGI(TAG, "ADXL345 inicializado");
        }
        else
        {
            ESP_LOGE(TAG, "Falha ao inicializar ADXL345");
            ok = false;
        }
    }

    return ok;
}

bool acquisitionStart()
{
    // Nucleo 1: deixa o nucleo 0 para o sistema (e depois para a inferencia)
    const BaseType_t result = xTaskCreatePinnedToCore(
        acquisitionTask, "acquisition", 4096, nullptr, 5, nullptr, 1);

    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Falha ao criar a tarefa de aquisicao");
        return false;
    }

    return true;
}