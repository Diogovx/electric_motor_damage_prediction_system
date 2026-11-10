#include "model_manager.h"

// Apenas configuracao e TFLite Micro do SDK. NAO incluir ei_run_classifier.h aqui:
// ele define as variaveis do modelo embutido e so pode entrar em um arquivo (inference.cpp).
#include "edge-impulse-sdk/classifier/ei_classifier_config.h"
#include "edge-impulse-sdk/tensorflow/lite/micro/micro_interpreter.h"
#include "edge-impulse-sdk/tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "edge-impulse-sdk/tensorflow/lite/schema/schema_generated.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef TFLITE_SCHEMA_VERSION
#define TFLITE_SCHEMA_VERSION 3
#endif

#define MODEL_MANAGER_LOG_PROBS 0
namespace
{
    constexpr char TAG[] = "MODEL_MGR";
    constexpr size_t DEFAULT_ARENA_BYTES = 8192;
    constexpr long MIN_MODEL_BYTES = 64;
    constexpr long MAX_MODEL_BYTES = 256 * 1024;

    // O interpreter guarda uma referencia ao resolver: ele precisa viver mais que o interpreter
    tflite::MicroMutableOpResolver<5>& opResolver()
    {
        static tflite::MicroMutableOpResolver<5> resolver;
        static bool ready = false;
        if (!ready)
        {
            resolver.AddFullyConnected();
            resolver.AddSoftmax();
            resolver.AddReshape();
            resolver.AddQuantize();
            resolver.AddDequantize();
            ready = true;
        }
        return resolver;
    }

    uint8_t* allocAligned(size_t size)
    {
        return static_cast<uint8_t*>(heap_caps_aligned_alloc(16, size, MALLOC_CAP_8BIT));
    }

    bool typeSupported(TfLiteType t)
    {
        return t == kTfLiteInt8 || t == kTfLiteFloat32;
    }

    size_t elementCount(const TfLiteTensor* t)
    {
        return t->bytes / (t->type == kTfLiteInt8 ? 1 : sizeof(float));
    }
}

ModelManager::~ModelManager()
{
    releaseInterpreter();
    heap_caps_free(modelBuffer);
    heap_caps_free(tensorArena);
}

void ModelManager::releaseInterpreter()
{
    delete interpreter;
    interpreter = nullptr;
    inputCount = 0;
    outputCount = 0;
}

bool ModelManager::allocateArena(size_t size)
{
    uint8_t* arena = allocAligned(size);
    if (arena == nullptr)
    {
        ESP_LOGE(TAG, "Sem memoria para a arena de %u bytes", (unsigned)size);
        return false;
    }

    releaseInterpreter();                // o interpreter aponta para a arena antiga
    heap_caps_free(tensorArena);
    tensorArena = arena;
    arenaSize = size;
    return true;
}

bool ModelManager::buildInterpreter()
{
    releaseInterpreter();

    if (modelBuffer == nullptr || tensorArena == nullptr)
    {
        return false;
    }

    const tflite::Model* model = tflite::GetModel(modelBuffer);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        ESP_LOGE(TAG, "Schema do modelo v%lu, esperado v%d",
                 (unsigned long)model->version(), (int)TFLITE_SCHEMA_VERSION);
        return false;
    }

    interpreter = new tflite::MicroInterpreter(
        model, opResolver(), tensorArena, arenaSize, nullptr, nullptr);

    if (interpreter->AllocateTensors(true) != kTfLiteOk)
    {
        ESP_LOGE(TAG, "AllocateTensors falhou (arena de %u bytes pequena ou operador nao suportado)",
                 (unsigned)arenaSize);
        releaseInterpreter();
        return false;
    }

    TfLiteTensor* in = interpreter->input(0);
    TfLiteTensor* out = interpreter->output(0);

    if (!typeSupported(in->type) || !typeSupported(out->type))
    {
        ESP_LOGE(TAG, "Tipo de tensor nao suportado (use int8 ou float32)");
        releaseInterpreter();
        return false;
    }

    inputCount = elementCount(in);
    outputCount = elementCount(out);

    ESP_LOGI(TAG, "Pronto: entrada %u valores (%s, scale=%.6f, zp=%d), saida %u classes",
             (unsigned)inputCount, in->type == kTfLiteInt8 ? "int8" : "float32",
             in->params.scale, (int)in->params.zero_point, (unsigned)outputCount);
    return true;
}

bool ModelManager::loadModelFromFS(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (f == nullptr)
    {
        ESP_LOGE(TAG, "Nao consegui abrir %s", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < MIN_MODEL_BYTES || size > MAX_MODEL_BYTES)
    {
        ESP_LOGE(TAG, "%s: tamanho invalido (%ld bytes)", path, size);
        fclose(f);
        return false;
    }

    uint8_t* buffer = allocAligned(static_cast<size_t>(size));
    if (buffer == nullptr)
    {
        ESP_LOGE(TAG, "Sem memoria para %ld bytes", size);
        fclose(f);
        return false;
    }

    const size_t got = fread(buffer, 1, static_cast<size_t>(size), f);
    fclose(f);

    if (got != static_cast<size_t>(size) || memcmp(buffer + 4, "TFL3", 4) != 0)
    {
        ESP_LOGE(TAG, "%s: leitura incompleta ou nao e um modelo TFLite", path);
        heap_caps_free(buffer);
        return false;
    }

    releaseInterpreter();
    heap_caps_free(modelBuffer);
    modelBuffer = buffer;
    modelSize = static_cast<size_t>(size);

    ESP_LOGI(TAG, "Modelo lido de %s (%u bytes)", path, (unsigned)modelSize);

    if (tensorArena == nullptr && !allocateArena(DEFAULT_ARENA_BYTES))
    {
        return false;
    }

    return buildInterpreter();
}

void ModelManager::reconfigureArena(size_t size)
{
    if (size < 1024)
    {
        ESP_LOGW(TAG, "Arena de %u bytes e pequena demais, ignorando", (unsigned)size);
        return;
    }

    if (!allocateArena(size))
    {
        return;
    }

    if (modelBuffer != nullptr)
    {
        buildInterpreter();              // refaz o interpreter com a nova arena
    }
}

int ModelManager::runInterference(float* input)
{
    if (interpreter == nullptr || input == nullptr)
    {
        return -1;
    }

    TfLiteTensor* in = interpreter->input(0);

    if (in->type == kTfLiteInt8)
    {
        for (size_t i = 0; i < inputCount; i++)
        {
            float q = roundf(input[i] / in->params.scale) + in->params.zero_point;
            q = fminf(127.0f, fmaxf(-128.0f, q));
            in->data.int8[i] = static_cast<int8_t>(q);
        }
    }
    else
    {
        memcpy(in->data.f, input, inputCount * sizeof(float));
    }

    const int64_t t0 = esp_timer_get_time();
    if (interpreter->Invoke() != kTfLiteOk)
    {
        ESP_LOGE(TAG, "Invoke falhou");
        return -1;
    }
    const int64_t elapsed = esp_timer_get_time() - t0;

    TfLiteTensor* out = interpreter->output(0);

    int best = 0;
    float bestValue = -1e30f;
    float probs[8] = {0};

    for (size_t i = 0; i < outputCount; i++)
    {
        const float v = (out->type == kTfLiteInt8)
            ? (out->data.int8[i] - out->params.zero_point) * out->params.scale
            : out->data.f[i];

        if (i < 8) probs[i] = v;
        if (v > bestValue)
        {
            bestValue = v;
            best = static_cast<int>(i);
        }
    }

#if MODEL_MANAGER_LOG_PROBS
    ESP_LOGI(TAG, "probs: %.3f %.3f %.3f | classe %d | invoke %lld us",
             probs[0], probs[1], probs[2], best, (long long)elapsed);
#endif
    return best;
}
