#include "inference.h"
#include "sample_stream.h"
#include "../model/model_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

// Este e o UNICO arquivo que inclui o SDK completo: ele define as variaveis do modelo embutido
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
using namespace ei;

// 1 = compara o modelo embutido com o modelo carregado do arquivo
#define EQUIVALENCE_TEST 1

namespace
{
    constexpr char TAG[] = "INFER";

    constexpr size_t AXES = 4;               // ax, ay, az, current_mA
    constexpr size_t STRIDE_FRAMES = 13;     // ~128 ms a 100 Hz
    constexpr size_t WINDOW_FRAMES = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    constexpr size_t NN_FEATURES = EI_CLASSIFIER_NN_INPUT_FRAME_SIZE;
    constexpr uint32_t EQ_EVERY_N_WINDOWS = 4;   // o DSP roda a mais nessas janelas

    static_assert(EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME == AXES, "O impulso precisa ter 4 eixos");
    static_assert(EI_CLASSIFIER_LABEL_COUNT == 3, "O log do teste assume 3 classes");

    ModelManager* dynamicModel = nullptr;

    float ring[WINDOW_FRAMES * AXES];        // buffer circular
    float window[WINDOW_FRAMES * AXES];      // janela linearizada (mais antiga primeiro)
    float nnFeatures[NN_FEATURES];           // as 52 features do DSP

#if EQUIVALENCE_TEST
    void compareWithDynamicModel(signal_t& signal, const ei_impulse_result_t& result)
    {
        // Mesmo bloco de DSP que o classificador embutido usa
        const ei_model_dsp_t& block = ei_dsp_blocks_1135680_1[0];
        matrix_t out(1, NN_FEATURES, nnFeatures);

        if (block.extract_fn(&signal, &out, block.config, EI_CLASSIFIER_FREQUENCY) != 0)
        {
            ESP_LOGE(TAG, "DSP falhou na comparacao");
            return;
        }

        ESP_LOGI(TAG, "EQ ref: %.4f %.4f %.4f",
                 result.classification[0].value,
                 result.classification[1].value,
                 result.classification[2].value);

        // O ModelManager registra a linha "probs: ..." logo em seguida
        dynamicModel->runInterference(nnFeatures);
    }
#endif

    void runWindow(uint32_t windowIndex)
    {
        signal_t signal;
        if (numpy::signal_from_buffer(window, WINDOW_FRAMES * AXES, &signal) != 0)
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

#if EQUIVALENCE_TEST
        if (dynamicModel != nullptr && windowIndex % EQ_EVERY_N_WINDOWS == 0)
        {
            compareWithDynamicModel(signal, result);
        }
#endif
    }

    void inferenceTask(void*)
    {
        size_t head = 0;                 // proxima posicao a escrever (= amostra mais antiga)
        size_t filled = 0;
        size_t sinceLast = 0;

        uint32_t windowCounter = 0;
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

            // Linearizar: da amostra mais antiga (head) ate a mais nova
            const size_t firstPart = (WINDOW_FRAMES - head) * AXES;
            memcpy(window, &ring[head * AXES], firstPart * sizeof(float));
            memcpy(window + firstPart, ring, head * AXES * sizeof(float));

            windowCounter++;
            windowsPerSecond++;
            runWindow(windowCounter);

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

bool inferenceStart(ModelManager* model)
{
    dynamicModel = model;

    const BaseType_t ok = xTaskCreatePinnedToCore(
        inferenceTask, "inference", 16384, nullptr, 4, nullptr, 0);

    if (ok != pdPASS)
    {
        ESP_LOGE(TAG, "Falha ao criar a tarefa de inferencia");
        return false;
    }
    return true;
}