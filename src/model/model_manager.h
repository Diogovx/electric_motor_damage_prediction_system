#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tflite { class MicroInterpreter; }

class ModelManager {
public:
    ModelManager() = default;
    ~ModelManager();
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    bool loadModelFromFS(const char* path);
    void reconfigureArena(size_t size);
    int  runInterference(float* input);     // indice da classe; -1 em erro

private:
    // detalhes de implementacao (nao precisam estar no diagrama)
    bool allocateArena(size_t size);
    bool buildInterpreter();
    void releaseInterpreter();

    uint8_t* modelBuffer = nullptr;
    uint8_t* tensorArena = nullptr;
    tflite::MicroInterpreter* interpreter = nullptr;

    size_t modelSize = 0;
    size_t arenaSize = 0;
    size_t inputCount = 0;
    size_t outputCount = 0;
};
