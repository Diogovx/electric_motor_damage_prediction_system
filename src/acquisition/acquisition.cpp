#include "acquisition.h"

#include "../config/system_config.h"
#include "../processing/sample_stream.h"
#include "../sensors/sensor_factory.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cmath>
#include <string.h>

static const char* TAG = "ACQ";

static constexpr TickType_t SAMPLE_PERIOD_TICKS = pdMS_TO_TICKS(10);  // 100 Hz
static constexpr int64_t REPORT_INTERVAL_US = 1000000;                // 1 s

static ISensor* adxlSensor = nullptr;
static ISensor* inaSensor = nullptr;

static void acquisitionTask(void*)
{
    const auto& config = getSensorConfiguration();

    float accel[3] = {0, 0, 0};   // ax, ay, az (m/s2)
    float current[1] = {0};       // mA

    uint32_t samples = 0;
    uint32_t inaErrors = 0;
    uint32_t adxlErrors = 0;
    uint32_t overruns = 0;

    TickType_t lastWake = xTaskGetTickCount();
    int64_t lastReport = esp_timer_get_time();

    ESP_LOGI(TAG, "Tarefa de aquisicao em execucao (nucleo %d)", xPortGetCoreID());

    while (true)
    {
        if (xTaskDelayUntil(&lastWake, SAMPLE_PERIOD_TICKS) == pdFALSE)
        {
            overruns++;
        }

        if (config.adxl345Enabled && adxlSensor != nullptr)
        {
            float tmp[3];
            adxlSensor->readData(tmp);
            if (std::isnan(tmp[0])) adxlErrors++;
            else memcpy(accel, tmp, sizeof(accel));   // em falha, mantem a ultima leitura boa
        }

        if (config.ina219Enabled && inaSensor != nullptr)
        {
            float tmp[1];
            inaSensor->readData(tmp);
            if (std::isnan(tmp[0])) inaErrors++;
            else current[0] = tmp[0];
        }

        samples++;

        const SampleFrame frame{accel[0], accel[1], accel[2], current[0]};
        sampleStreamPush(frame);

        const int64_t now = esp_timer_get_time();
        if (now - lastReport >= REPORT_INTERVAL_US)
        {
            ESP_LOGI(TAG, "%lu amostras/s | erros INA=%lu ADXL=%lu | atrasos=%lu",
                     (unsigned long)samples, (unsigned long)inaErrors,
                     (unsigned long)adxlErrors, (unsigned long)overruns);
            ESP_LOGI(TAG, "ultima: I=%.1f mA | a=(%.3f, %.3f, %.3f) m/s2",
                     current[0], accel[0], accel[1], accel[2]);

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
    SensorFactory factory;
    bool ok = true;

    if (config.ina219Enabled)
    {
        inaSensor = factory.createSensor(SensorType::INA219);
        if (inaSensor == nullptr || !inaSensor->init())
        {
            ESP_LOGE(TAG, "Falha ao inicializar INA219");
            ok = false;
        }
    }

    if (config.adxl345Enabled)
    {
        adxlSensor = factory.createSensor(SensorType::ADXL345);
        if (adxlSensor == nullptr || !adxlSensor->init())
        {
            ESP_LOGE(TAG, "Falha ao inicializar ADXL345");
            ok = false;
        }
    }

    return ok;
}

bool acquisitionStart()
{
    static TaskHandle_t handle = nullptr;

    if (handle != nullptr)
    {
        ESP_LOGW(TAG, "Aquisicao ja iniciada, ignorando");
        return true;
    }

    // Nucleo 1: deixa o nucleo 0 para o sistema e a inferencia
    const BaseType_t result = xTaskCreatePinnedToCore(
        acquisitionTask, "acquisition", 4096, nullptr, 5, &handle, 1);

    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Falha ao criar a tarefa de aquisicao");
        handle = nullptr;
        return false;
    }

    return true;
}