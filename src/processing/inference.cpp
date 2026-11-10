#include "inference.h"
#include "sample_stream.h"
#include "../dsp/feature_extractor.h"
#include "../model/model_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

namespace
{
    constexpr char TAG[] = "INFER";

    // Provisorio: estes valores passam a vir do config.json no SystemController
    constexpr size_t AXES = 4;                  // ax, ay, az, current_mA
    constexpr size_t WINDOW_FRAMES = 256;       // 2560 ms a 100 Hz
    constexpr size_t STRIDE_FRAMES = 13;        // ~128 ms
    constexpr size_t NN_FEATURES = 52;
    constexpr float SAMPLE_RATE_HZ = 100.0f;

    // Ordem das saidas do modelo (conferida com ei_classifier_inferencing_categories)
    const char* const LABELS[] = {"Desbalanceamento", "Falha de Engrenagem", "Saudavel"};
    constexpr int LABEL_COUNT = sizeof(LABELS) / sizeof(LABELS[0]);

    ModelManager* model = nullptr;
    DspParams dspParams;

    float ring[WINDOW_FRAMES * AXES];           // buffer circular
    float window[WINDOW_FRAMES * AXES];         // janela linearizada (mais antiga primeiro)
    float features[NN_FEATURES];

    void runWindow()
    {
        const int64_t t0 = esp_timer_get_time();
        if (!extractFeatures(window, WINDOW_FRAMES, AXES, SAMPLE_RATE_HZ,
                             dspParams, features, NN_FEATURES))
        {
            ESP_LOGE(TAG, "Falha ao extrair as features");
            return;
        }
        const int64_t t1 = esp_timer_get_time();

        const int cls = model->runInterference(features);
        const int64_t t2 = esp_timer_get_time();

        if (cls < 0 || cls >= LABEL_COUNT)
        {
            ESP_LOGE(TAG, "Classe invalida: %d", cls);
            return;
        }

        ESP_LOGI(TAG, "%s | DSP %lld ms, NN %lld us",
                 LABELS[cls], (long long)((t1 - t0) / 1000), (long long)(t2 - t1));
    }

    void inferenceTask(void*)
    {
        size_t head = 0;                 // proxima posicao a escrever (= amostra mais antiga)
        size_t filled = 0;
        size_t sinceLast = 0;

        uint32_t windowsPerSecond = 0;
        int64_t lastReport = esp_timer_get_time();

        ESP_LOGI(TAG, "Tarefa de inferencia em execucao (nucleo %d)", xPortGetCoreID());

        while (true)
        {
            SampleFrame f;
            if (!sampleStreamPop(f, 1000))
            {
                ESP_LOGW(TAG, "Sem amostras ha 1 s");
                continue;
            }

            float* slot = &ring[head * AXES];
            slot[0] = f.ax;
            slot[1] = f.ay;
            slot[2] = f.az;
            slot[3] = f.current_mA;

            head = (head + 1) % WINDOW_FRAMES;
            if (filled < WINDOW_FRAMES) filled++;
            sinceLast++;

            if (filled < WINDOW_FRAMES || sinceLast < STRIDE_FRAMES)
            {
                continue;
            }
            sinceLast = 0;

            if (model == nullptr)
            {
                continue;                // sem modelo: so drena a fila
            }

            // Linearizar: da amostra mais antiga (head) ate a mais nova
            const size_t firstPart = (WINDOW_FRAMES - head) * AXES;
            memcpy(window, &ring[head * AXES], firstPart * sizeof(float));
            memcpy(window + firstPart, ring, head * AXES * sizeof(float));

            windowsPerSecond++;
            runWindow();

            const int64_t now = esp_timer_get_time();
            if (now - lastReport >= 1000000)
            {
                ESP_LOGI(TAG, "%lu janelas/s | amostras perdidas=%lu",
                         (unsigned long)windowsPerSecond,
                         (unsigned long)sampleStreamDropped());
                windowsPerSecond = 0;
                lastReport = now;
            }
        }
    }
}

bool inferenceStart(ModelManager* m)
{
    model = m;
    if (model == nullptr)
    {
        ESP_LOGW(TAG, "Sem modelo carregado: a aquisicao segue, sem classificacao");
    }

    // Pilha grande: o DSP espectral usa bastante pilha
    const BaseType_t ok = xTaskCreatePinnedToCore(
        inferenceTask, "inference", 16384, nullptr, 4, nullptr, 0);

    if (ok != pdPASS)
    {
        ESP_LOGE(TAG, "Falha ao criar a tarefa de inferencia");
        return false;
    }
    return true;
}