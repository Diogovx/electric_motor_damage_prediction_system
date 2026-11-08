#include "config/system_config.h"
#include "acquisition/acquisition.h"
#include "drivers/i2c_bus.h"
#include "processing/sample_stream.h"
#include "processing/inference.h"
#include "model/model_store.h"

#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "MAIN";

// Apenas durante o desenvolvimento: espera o monitor reconectar apos o reset
// da USB nativa. Coloque 0 na versao final.
static constexpr uint32_t BOOT_LOG_DELAY_MS = 10000;

static constexpr uint8_t ADXL345_ADDR = 0x53;
static constexpr uint8_t INA219_ADDR = 0x40;

[[noreturn]] static void fatalError(const char* message)
{
    ESP_LOGE(TAG, "ERRO FATAL: %s", message);
    ESP_LOGE(TAG, "Reiniciando em 5 s...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}

static void checkDevice(const char* name, uint8_t address, bool enabled)
{
    if (!enabled)
    {
        ESP_LOGI(TAG, "%s desabilitado na configuracao", name);
        return;
    }

    if (i2cBusProbe(address))
    {
        ESP_LOGI(TAG, "%s respondeu em 0x%02X", name, address);
    }
    else
    {
        ESP_LOGW(TAG, "%s NAO respondeu em 0x%02X (fiacao, endereco ou pull-up?)",
                 name, address);
    }
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
    ESP_LOGI(TAG, "ESP32-S3 | %d nucleos | IDF %s", chip.cores, esp_get_idf_version());
    ESP_LOGI(TAG, "Heap livre: %lu bytes", (unsigned long)esp_get_free_heap_size());

    initSystemConfiguration();
    const auto& config = getSensorConfiguration();

    const esp_err_t err = i2cBusInit();
    ModelInfo model{};
    if (!modelStoreOpen(0, model))
    {
        ESP_LOGW(TAG, "Sem modelo valido no slot A: a aquisicao continua, sem inferencia");
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C: %s", esp_err_to_name(err));
        fatalError("falha ao inicializar o barramento I2C");
    }

    checkDevice("ADXL345", ADXL345_ADDR, config.adxl345Enabled);
    checkDevice("INA219", INA219_ADDR, config.ina219Enabled);

    if (!acquisitionInit())
    {
        fatalError("falha ao inicializar os sensores");
    }

    if (!acquisitionStart())
    {
        fatalError("falha ao iniciar a aquisicao");
    }

    ESP_LOGI(TAG, "Sistema em execucao. Heap livre: %lu bytes",
             (unsigned long)esp_get_free_heap_size());

    if (!sampleStreamInit())
    {
        fatalError("falha ao criar a fila de amostras");
    }

    if (!inferenceStart())
    {
        fatalError("falha ao iniciar a inferencia");
    }

    
}