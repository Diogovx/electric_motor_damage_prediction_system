#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "../dsp/feature_extractor.h"
#include "../sensors/sensor_factory.h"

constexpr size_t MAX_AXES = 8;
constexpr size_t MAX_WINDOW_FRAMES = 512;
constexpr size_t MAX_FEATURES = 256;
constexpr size_t MAX_LABELS = 8;
constexpr size_t MAX_SENSORS = 4;
constexpr size_t MAX_MODEL_BYTES = 64 * 1024;

struct SensorSpec
{
    SensorType type = SensorType::ADXL345;
    int channels = 0;
    int rangeG = 16;
};

struct RuntimeConfig
{
    int version = 0;
    std::string motor;
    std::vector<SensorSpec> sensors;        // a ordem e a das colunas usadas no treino

    int samplingHz = 100;
    int windowMs = 2560;
    int strideMs = 128;

    DspParams dsp;

    size_t arenaBytes = 8192;
    size_t features = 52;
    std::vector<std::string> labels;        // na ordem das saidas do modelo
    uint32_t modelSize = 0;                 // 0 = nao informado
    uint32_t modelCrc32 = 0;                // 0 = nao informado

    // derivados
    size_t axes = 0;
    size_t windowFrames = 0;
    size_t strideFrames = 0;
};

// Valida e converte o objeto "config" do JSON. Em erro, devolve false e a causa em err.
bool parseRuntimeConfig(JsonObjectConst cfg, RuntimeConfig& out, std::string& err);
