#include "inference.h"
#include "sample_stream.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define ENABLE_INFERENCE 1

#ifdef ENABLE_INFERENCE
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
using namespace ei;
#endif

namespace
{
    constexpr char TAG[] = "INFER";

    constexpr size_t AXES = 4;             // ax, ay, az, current_mA (ordem do impulso)
    constexpr size_t STRIDE_FRAMES = 13;   // ~128 ms a 100 Hz

#ifdef ENABLE_INFERENCE
    constexpr size_t WINDOW_FRAMES = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    static_assert(EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME == AXES,
                  "O impulso precisa ter 4 eixos");
#else
    constexpr size_t WINDOW_FRAMES = 256;  // 2560 ms a 100 Hz
#endif

    float ring[WINDOW_FRAMES * AXES];      // buffer circular
    float features[WINDOW_FRAMES * AXES];  // janela linearizada (mais antiga primeiro)

    void runWindow()
    {
#ifdef ENABLE_INFERENCE
        signal_t signal;
        if (numpy::signal_from_buffer(features, WINDOW_FRAMES * AXES, &signal) != 0)
        {
            ESP_LOGE(TAG, "Falha ao criar o signal");
            return;
        }

        ei_impulse_result_t result = {};
        const EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
        if (err != EI_IMPULSE_OK)
        {
            ESP_LOGE(TAG, "run_classifier falhou: %d", static_cast<int>(err));
            return;
        }

        size_t best = 0;
        for (size_t i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++)
        {
            if (result.classification[i].value > result.classification[best].value)
            {
                best = i;
            }
        }

        ESP_LOGI(TAG, "%s (%.1f%%) | DSP %d ms, NN %d ms",
                 result.classification[best].label,
                 result.classification[best].value * 100.0f,
                 static_cast<int>(result.timing.dsp),
                 static_cast<int>(result.timing.classification));
#endif
    }

    void inferenceTask(void*)
    {
        size_t head = 0;        // proxima posicao a escrever (= amostra mais antiga)
        size_t filled = 0;
        size_t sinceLast = 0;

        uint32_t windows = 0;
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

            // Linearizar: da amostra mais antiga (head) ate a mais nova
            const size_t firstPart = (WINDOW_FRAMES - head) * AXES;
            memcpy(features, &ring[head * AXES], firstPart * sizeof(float));
            memcpy(features + firstPart, ring, head * AXES * sizeof(float));

            windows++;
            runWindow();

            const int64_t now = esp_timer_get_time();
            if (now - lastReport >= 1000000)
            {
                ESP_LOGI(TAG, "%lu janelas/s | amostras perdidas=%lu",
                         (unsigned long)windows,
                         (unsigned long)sampleStreamDropped());
                windows = 0;
                lastReport = now;
            }
        }
    }
}

bool inferenceStart()
{
    // Pilha grande: o DSP espectral e o TFLite usam bastante pilha
    const BaseType_t ok = xTaskCreatePinnedToCore(
        inferenceTask, "inference", 16384, nullptr, 4, nullptr, 0);

    if (ok != pdPASS)
    {
        ESP_LOGE(TAG, "Falha ao criar a tarefa de inferencia");
        return false;
    }
    return true;
}