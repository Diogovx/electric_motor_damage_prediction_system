#include "drivers/i2c_bus.h"
#include "storage/storage.h"
#include "system/net_link.h"
#include "system/system_controller.h"

#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "MAIN";

// Apenas durante o desenvolvimento: espera o monitor reconectar apos o reset
// da USB nativa. Coloque 0 na versao final e nas medicoes de tempo.
static constexpr uint32_t BOOT_LOG_DELAY_MS = 10000;

static SystemController controller;      // objeto grande: fica fora da pilha

[[noreturn]] static void fatalError(const char* message)
{
    ESP_LOGE(TAG, "ERRO FATAL: %s", message);
    ESP_LOGE(TAG, "Reiniciando em 5 s...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}

static void controllerTask(void*)
{
    controller.mainLoop();
    vTaskDelete(nullptr);
}

extern "C" void app_main(void)
{
    if (BOOT_LOG_DELAY_MS > 0)
    {
        vTaskDelay(pdMS_TO_TICKS(BOOT_LOG_DELAY_MS));
    }

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "=== Diagnostico de falhas em motores (TinyML) ===");
    ESP_LOGI(TAG, "ESP32-S3 | %d nucleos | IDF %s | heap livre %lu",
             chip.cores, esp_get_idf_version(), static_cast<unsigned long>(esp_get_free_heap_size()));

    if (i2cBusInit() != ESP_OK)
    {
        fatalError("falha ao inicializar o barramento I2C");
    }
    if (storageInit() != ESP_OK)
    {
        fatalError("falha ao montar o sistema de arquivos");
    }

    // O controlador roda numa tarefa propria (pilha maior que a da main)
    xTaskCreate(controllerTask, "controller", 12288, nullptr, 3, nullptr);

    // Wi-Fi e MQTT (entrega os payloads de reconfiguracao ao controlador)
    netLinkStart();
}
