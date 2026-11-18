#pragma once

#include <atomic>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "runtime_config.h"
#include "../model/model_manager.h"
#include "../sensors/i_sensor.h"

// Rede -> controlador: copia o JSON recebido (pode ser chamada de qualquer tarefa)
bool systemSubmitPayload(const char* json, size_t len, uint32_t rxFirstUs, uint32_t rxDoneUs);
// O MQTT usa isto para publicar o estado (opcional)
void systemSetStatusSink(void (*sink)(const char* json));

class SystemController {
public:
    void onMqttConfigReceived(const JsonDocument& payload);
    void reconfigureRuntime();
    void mainLoop();                              // nao retorna

private:
    struct Frame { float v[MAX_AXES]; };
    struct Pending { std::string json; uint32_t rxFirstUs; uint32_t rxDoneUs; };

    friend bool systemSubmitPayload(const char*, size_t, uint32_t, uint32_t);

    static void acqEntry(void* self);
    static void infEntry(void* self);
    void acquisitionTask();
    void inferenceTask();
    void runWindow();
    bool selfTest();
    void publishStatus(const char* state, const char* detail);
    void pollTestTrigger();
    void pollSummary();

    // ---- diagrama ----
    std::vector<ISensor*> sensors;                // diagrama: currentSensor (lista, 1..*)
    ModelManager modelMgr;
    std::string configPath = "/storage/config.json";

    // ---- estado de execucao ----
    RuntimeConfig cfg;
    bool running = false;
    bool lastApplyOk = false;
    std::atomic<bool> pauseReq{true};

    QueueHandle_t frameQueue = nullptr;
    QueueHandle_t inbox = nullptr;
    EventGroupHandle_t parkEvents = nullptr;

    float lastGood[MAX_AXES] = {0};
    float ring[MAX_WINDOW_FRAMES * MAX_AXES];
    float window[MAX_WINDOW_FRAMES * MAX_AXES];
    float features[MAX_FEATURES];

    // ---- medicao de tempo (us, 32 bits: diferencas validas por ~71 min) ----
    uint32_t tRxFirst = 0, tRxDone = 0, tFilesOk = 0, tOldReleased = 0;
    uint32_t tSensorsReady = 0, tModelReady = 0, tSelfTest = 0, tLastOld = 0;
    std::atomic<uint32_t> tFirstSample{0}, tFirstInference{0}, tLastInference{0};
    bool summaryPending = false;
    int trialId = 0;
    std::string trialTag;
};
