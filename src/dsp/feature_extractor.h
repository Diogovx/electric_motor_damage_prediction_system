#pragma once
#include <stddef.h>
#include <string>

// Parametros do DSP espectral. Os padroes sao os do impulso exportado;
// no SystemController virao do config.json (secao "dsp").
struct DspParams
{
    int fftLength = 16;
    int peaks = 3;
    float peaksThreshold = 0.1f;
    std::string powerEdges = "0.1, 0.5, 1.0, 2.0, 5.0";
    bool doLog = true;
    bool fftOverlap = true;
    std::string filterType = "none";
    float filterCutoff = 3.0f;
    int filterOrder = 6;
    float scaleAxes = 1.0f;
    int decimation = 1;
};

// window: frames x axes, intercalado (ax, ay, az, I, ax, ay, ...)
bool extractFeatures(const float* window, size_t frames, size_t axes,
                     float sampleRateHz, const DspParams& params,
                     float* out, size_t outCount);
