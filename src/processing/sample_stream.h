#pragma once

#include <stdint.h>

struct SampleFrame
{
    float ax;
    float ay;
    float az;
    float current_mA;
};

bool sampleStreamInit();

// Nao bloqueante: usada pela tarefa de aquisicao
bool sampleStreamPush(const SampleFrame& frame);

// Bloqueia ate chegar uma amostra ou estourar o timeout
bool sampleStreamPop(SampleFrame& frame, uint32_t timeoutMs);

uint32_t sampleStreamDropped();