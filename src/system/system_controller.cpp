#include "system_controller.h"

#include "../dsp/feature_extractor.h"
#include "../sensors/adxl345_adapter.h"
#include "../sensors/sensor_factory.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/task.h"

#include <cmath>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

namespace
{
    constexpr char TAG[] = "CTRL";
    constexpr char TAG_ACQ[] = "ACQ";
    constexpr char TAG_INF[] = "INFER";
    constexpr char TAG_EVT[] = "RECONF";

    constexpr const char* MODEL_PATH       = "/storage/model.tflite";
    constexpr const char* MODEL_PATH_PREV  = "/storage/model.prev.tflite";
    constexpr const char* MODEL_PATH_TMP   = "/storage/model.tmp";
    constexpr const char* CONFIG_PATH_PREV = "/storage/config.prev.json";
    constexpr const char* CONFIG_PATH_TMP  = "/storage/config.tmp";
    constexpr const char* TEST_PAYLOAD[2]  = {"/storage/test_a.json", "/storage/test_b.json"};

    constexpr EventBits_t BIT_ACQ_PARKED = (1 << 0);
    constexpr EventBits_t BIT_INF_PARKED = (1 << 1);

    SystemController* g_controller = nullptr;
    void (*g_statusSink)(const char*) = nullptr;

    uint32_t nowUs() { return static_cast<uint32_t>(esp_timer_get_time()); }

    void logEventAt(const char* name, uint32_t t)
    {
        ESP_LOGI(TAG_EVT, "ev=%s t_us=%lu", name, static_cast<unsigned long>(t));
    }

    uint32_t markEvent(const char* name)
    {
        const uint32_t t = nowUs();
        logEventAt(name, t);
        return t;
    }

    bool fileExists(const char* path)
    {
        struct stat st;
        return stat(path, &st) == 0;
    }

    bool readFile(const char* path, std::string& out)
    {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size < 0 || size > 128 * 1024)
        {
            fclose(f);
            return false;
        }
        out.resize(static_cast<size_t>(size));
        const size_t got = size ? fread(&out[0], 1, static_cast<size_t>(size), f) : 0;
        fclose(f);
        return got == static_cast<size_t>(size);
    }

    bool writeFile(const char* path, const uint8_t* data, size_t len)
    {
        FILE* f = fopen(path, "wb");
        if (!f) return false;
        const size_t w = fwrite(data, 1, len, f);
        fflush(f);
        fclose(f);
        return w == len;
    }

    uint32_t crc32Of(const void* data, size_t len)
    {
        return esp_rom_crc32_le(0, static_cast<const uint8_t*>(data), static_cast<uint32_t>(len));
    }

    int b64val(char c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    bool base64Decode(const char* in, std::vector<uint8_t>& out)
    {
        out.clear();
        uint32_t acc = 0;
        int bits = 0;
        for (const char* p = in; *p; ++p)
        {
            if (*p == '=') break;
            if (*p == '\n' || *p == '\r' || *p == ' ') continue;
            const int v = b64val(*p);
            if (v < 0) return false;
            acc = (acc << 6) | static_cast<uint32_t>(v);
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
            }
        }
        return !out.empty();
    }
}

bool systemSubmitPayload(const char* json, size_t len, uint32_t rxFirstUs, uint32_t rxDoneUs)
{
    if (!g_controller || !g_controller->inbox) return false;
    auto* p = new SystemController::Pending{std::string(json, len), rxFirstUs, rxDoneUs};
    if (xQueueSend(g_controller->inbox, &p, 0) != pdTRUE)
    {
        delete p;
        return false;
    }
    return true;
}

void systemSetStatusSink(void (*sink)(const char* json))
{
    g_statusSink = sink;
}

void SystemController::publishStatus(const char* state, const char* detail)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"state\":\"%s\",\"version\":%d,\"motor\":\"%s\",\"trial\":%d,\"detail\":\"%s\"}",
             state, cfg.version, cfg.motor.c_str(), trialId, detail ? detail : "");
    ESP_LOGI(TAG, "STATUS %s", buf);
    if (g_statusSink) g_statusSink(buf);
}

// ---------------------------------------------------------------------------
// Tarefas de aquisicao (nucleo 1) e inferencia (nucleo 0)
// ---------------------------------------------------------------------------

void SystemController::acqEntry(void* self)
{
    static_cast<SystemController*>(self)->acquisitionTask();
}

void SystemController::infEntry(void* self)
{
    static_cast<SystemController*>(self)->inferenceTask();
}

void SystemController::acquisitionTask()
{
    TickType_t last = xTaskGetTickCount();
    uint32_t samples = 0, invalid = 0, overruns = 0;
    int64_t lastReport = esp_timer_get_time();

    for (;;)
    {
        if (pauseReq.load())
        {
            while (pauseReq.load())
            {
                xEventGroupSetBits(parkEvents, BIT_ACQ_PARKED);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            xEventGroupClearBits(parkEvents, BIT_ACQ_PARKED);
            last = xTaskGetTickCount();
            samples = invalid = overruns = 0;
            lastReport = esp_timer_get_time();
            continue;
        }

        const TickType_t period = pdMS_TO_TICKS(1000 / cfg.samplingHz);
        if (xTaskDelayUntil(&last, period) == pdFALSE) overruns++;

        Frame f{};
        size_t k = 0;
        for (size_t s = 0; s < sensors.size(); s++)
        {
            float tmp[MAX_AXES];
            sensors[s]->readData(tmp);
            const int ch = cfg.sensors[s].channels;
            for (int c = 0; c < ch && k < MAX_AXES; c++, k++)
            {
                if (std::isnan(tmp[c]))
                {
                    invalid++;
                    f.v[k] = lastGood[k];             // em falha, mantem a ultima leitura boa
                }
                else
                {
                    f.v[k] = tmp[c];
                    lastGood[k] = tmp[c];
                }
            }
        }

        xQueueSend(frameQueue, &f, 0);
        samples++;

        if (tFirstSample.load() == 0)
        {
            tFirstSample.store(markEvent("first_sample"));
        }

        const int64_t now = esp_timer_get_time();
        if (now - lastReport >= 1000000)
        {
            ESP_LOGI(TAG_ACQ, "%lu amostras/s | leituras invalidas=%lu | atrasos=%lu",
                     static_cast<unsigned long>(samples), static_cast<unsigned long>(invalid),
                     static_cast<unsigned long>(overruns));
            samples = invalid = overruns = 0;
            lastReport = now;
        }
    }
}

void SystemController::inferenceTask()
{
    size_t head = 0, filled = 0, sinceLast = 0;
    uint32_t windowsPerSecond = 0;
    int64_t lastReport = esp_timer_get_time();

    for (;;)
    {
        if (pauseReq.load())
        {
            while (pauseReq.load())
            {
                xEventGroupSetBits(parkEvents, BIT_INF_PARKED);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            xEventGroupClearBits(parkEvents, BIT_INF_PARKED);
            head = filled = sinceLast = 0;
            windowsPerSecond = 0;
            lastReport = esp_timer_get_time();
            continue;
        }

        Frame f;
        if (xQueueReceive(frameQueue, &f, pdMS_TO_TICKS(50)) != pdTRUE) continue;

        const size_t axes = cfg.axes;
        const size_t wf = cfg.windowFrames;

        memcpy(&ring[head * axes], f.v, axes * sizeof(float));
        head = (head + 1) % wf;
        if (filled < wf) filled++;
        sinceLast++;

        if (filled < wf || sinceLast < cfg.strideFrames) continue;
        sinceLast = 0;

        // linearizar: da amostra mais antiga (head) ate a mais nova
        const size_t first = (wf - head) * axes;
        memcpy(window, &ring[head * axes], first * sizeof(float));
        memcpy(window + first, ring, head * axes * sizeof(float));

        windowsPerSecond++;
        runWindow();

        const int64_t now = esp_timer_get_time();
        if (now - lastReport >= 1000000)
        {
            ESP_LOGI(TAG_INF, "%lu janelas/s", static_cast<unsigned long>(windowsPerSecond));
            windowsPerSecond = 0;
            lastReport = now;
        }
    }
}

void SystemController::runWindow()
{
    const int64_t t0 = esp_timer_get_time();
    if (!extractFeatures(window, cfg.windowFrames, cfg.axes, static_cast<float>(cfg.samplingHz),
                         cfg.dsp, features, cfg.features))
    {
        ESP_LOGE(TAG_INF, "Falha ao extrair as features");
        return;
    }
    const int64_t t1 = esp_timer_get_time();

    const int cls = modelMgr.runInterference(features);
    const int64_t t2 = esp_timer_get_time();

    if (cls < 0 || cls >= static_cast<int>(cfg.labels.size()))
    {
        ESP_LOGE(TAG_INF, "Classe invalida: %d", cls);
        return;
    }

    const uint32_t t = nowUs();
    tLastInference.store(t);
    uint32_t expected = 0;
    if (tFirstInference.compare_exchange_strong(expected, t))
    {
        logEventAt("first_inference_valid", t);
    }

    ESP_LOGI(TAG_INF, "%s | DSP %lld ms, NN %lld us", cfg.labels[cls].c_str(),
             static_cast<long long>((t1 - t0) / 1000), static_cast<long long>(t2 - t1));
}

// Autoteste: sinal sintetico -> DSP -> modelo. Confirma que o par DSP+modelo roda e responde.
bool SystemController::selfTest()
{
    for (size_t i = 0; i < cfg.windowFrames; i++)
    {
        for (size_t a = 0; a < cfg.axes; a++)
        {
            window[i * cfg.axes + a] = sinf(0.3f * static_cast<float>(i) + static_cast<float>(a)) +
                                       (a == 2 ? 10.0f : 0.0f);
        }
    }

    if (!extractFeatures(window, cfg.windowFrames, cfg.axes, static_cast<float>(cfg.samplingHz),
                         cfg.dsp, features, cfg.features))
    {
        return false;
    }
    for (size_t i = 0; i < cfg.features; i++)
    {
        if (!std::isfinite(features[i])) return false;
    }

    const int cls = modelMgr.runInterference(features);
    return cls >= 0 && cls < static_cast<int>(cfg.labels.size());
}

// ---------------------------------------------------------------------------
// Reconfiguracao: le a configuracao ativa e a aplica (com as tarefas paradas)
// ---------------------------------------------------------------------------

void SystemController::reconfigureRuntime()
{
    lastApplyOk = false;

    // 1) ler e validar (nada muda se falhar aqui)
    std::string text;
    if (!readFile(configPath.c_str(), text))
    {
        ESP_LOGE(TAG, "Nao consegui ler %s", configPath.c_str());
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, text))
    {
        ESP_LOGE(TAG, "JSON invalido em %s", configPath.c_str());
        return;
    }
    RuntimeConfig next;
    std::string err;
    if (!parseRuntimeConfig(doc.as<JsonObjectConst>(), next, err))
    {
        ESP_LOGE(TAG, "Configuracao invalida: %s", err.c_str());
        return;
    }

    // 2) integridade do modelo gravado
    {
        std::string bytes;
        if (!readFile(MODEL_PATH, bytes) || bytes.size() < 64)
        {
            ESP_LOGE(TAG, "Nao consegui ler %s", MODEL_PATH);
            return;
        }
        if (next.modelCrc32 != 0 && crc32Of(bytes.data(), bytes.size()) != next.modelCrc32)
        {
            ESP_LOGE(TAG, "CRC32 do modelo nao confere");
            return;
        }
    }

    // 3) parar as tarefas
    tLastOld = tLastInference.load();
    markEvent("reconf_begin");
    logEventAt("last_inference_old", tLastOld);

    xEventGroupClearBits(parkEvents, BIT_ACQ_PARKED | BIT_INF_PARKED);
    pauseReq = true;
    const EventBits_t bits = xEventGroupWaitBits(parkEvents, BIT_ACQ_PARKED | BIT_INF_PARKED,
                                                 pdFALSE, pdTRUE, pdMS_TO_TICKS(2000));
    if ((bits & (BIT_ACQ_PARKED | BIT_INF_PARKED)) != (BIT_ACQ_PARKED | BIT_INF_PARKED))
    {
        ESP_LOGE(TAG, "As tarefas nao pararam a tempo");
        pauseReq = !running;                  // se ja rodava, retoma como estava
        return;
    }

    // 4) liberar o anterior
    for (ISensor* s : sensors) delete s;
    sensors.clear();
    running = false;
    tOldReleased = markEvent("old_released");

    // 5) criar e iniciar os novos sensores
    SensorFactory factory;
    for (const SensorSpec& spec : next.sensors)
    {
        ISensor* s = factory.createSensor(spec.type);
        if (!s)
        {
            ESP_LOGE(TAG, "Factory nao criou o sensor");
            return;
        }
        if (spec.type == SensorType::ADXL345)
        {
            static_cast<ADXL345Adapter*>(s)->setRange(static_cast<uint8_t>(spec.rangeG));
        }
        if (!s->init())
        {
            delete s;
            ESP_LOGE(TAG, "Falha ao iniciar um sensor");
            return;
        }
        if (s->getSamplingRate() < static_cast<uint32_t>(next.samplingHz))
        {
            ESP_LOGW(TAG, "Taxa nativa do sensor menor que sampling_hz");
        }
        sensors.push_back(s);
    }
    tSensorsReady = markEvent("sensors_ready");

    // 6) modelo (o ModelManager libera o interpretador e o modelo antigos)
    modelMgr.reconfigureArena(next.arenaBytes);
    if (!modelMgr.loadModelFromFS(MODEL_PATH))
    {
        ESP_LOGE(TAG, "Falha ao carregar o modelo");
        return;
    }
    tModelReady = markEvent("model_ready");

    // 7) autoteste com a nova configuracao
    cfg = next;
    if (!selfTest())
    {
        ESP_LOGE(TAG, "Autoteste falhou");
        return;
    }
    tSelfTest = markEvent("selftest_ok");

    // 8) retomar com buffers limpos
    xQueueReset(frameQueue);
    memset(lastGood, 0, sizeof(lastGood));
    tFirstSample.store(0);
    tFirstInference.store(0);
    summaryPending = true;
    running = true;
    lastApplyOk = true;
    pauseReq = false;
    publishStatus("running", "configuracao aplicada");
}

// ---------------------------------------------------------------------------
// Entrada de uma nova configuracao (payload do MQTT ou do gatilho de teste)
// ---------------------------------------------------------------------------

void SystemController::onMqttConfigReceived(const JsonDocument& payload)
{
    trialId = payload["trial"] | 0;
    trialTag = payload["tag"] | "";
    logEventAt("rx_first", tRxFirst);
    logEventAt("rx_done", tRxDone);

    if (!payload["config"].is<JsonObjectConst>())
    {
        publishStatus("rejected", "campo 'config' ausente");
        return;
    }

    RuntimeConfig probe;
    std::string err;
    if (!parseRuntimeConfig(payload["config"].as<JsonObjectConst>(), probe, err))
    {
        publishStatus("rejected", err.c_str());
        return;
    }
    if (probe.modelSize == 0 || probe.modelCrc32 == 0)
    {
        publishStatus("rejected", "model.size e model.crc32 sao obrigatorios");
        return;
    }

    std::vector<uint8_t> model;
    const char* b64 = payload["model_b64"] | "";
    if (!base64Decode(b64, model))
    {
        publishStatus("rejected", "model_b64 invalido");
        return;
    }
    if (model.size() != probe.modelSize || model.size() < 64 || model.size() > MAX_MODEL_BYTES)
    {
        publishStatus("rejected", "tamanho do modelo nao confere");
        return;
    }
    if (crc32Of(model.data(), model.size()) != probe.modelCrc32)
    {
        publishStatus("rejected", "CRC32 do modelo nao confere");
        return;
    }
    if (memcmp(model.data() + 4, "TFL3", 4) != 0)
    {
        publishStatus("rejected", "arquivo nao e um modelo TFLite");
        return;
    }

    std::string cfgText;
    serializeJson(payload["config"], cfgText);

    // gravar em temporarios e conferir a releitura
    if (!writeFile(MODEL_PATH_TMP, model.data(), model.size()) ||
        !writeFile(CONFIG_PATH_TMP, reinterpret_cast<const uint8_t*>(cfgText.data()), cfgText.size()))
    {
        publishStatus("rejected", "falha ao gravar no LittleFS");
        return;
    }
    {
        std::string back;
        if (!readFile(MODEL_PATH_TMP, back) || back.size() != model.size() ||
            crc32Of(back.data(), back.size()) != probe.modelCrc32)
        {
            publishStatus("rejected", "releitura do modelo gravado nao confere");
            return;
        }
    }

    // guardar o anterior e ativar o novo
    remove(MODEL_PATH_PREV);
    remove(CONFIG_PATH_PREV);
    const bool hadActive = fileExists(MODEL_PATH) && fileExists(configPath.c_str());
    if (hadActive)
    {
        rename(MODEL_PATH, MODEL_PATH_PREV);
        rename(configPath.c_str(), CONFIG_PATH_PREV);
    }
    if (rename(MODEL_PATH_TMP, MODEL_PATH) != 0 || rename(CONFIG_PATH_TMP, configPath.c_str()) != 0)
    {
        publishStatus("rejected", "falha ao ativar os arquivos novos");
        if (hadActive)
        {
            remove(MODEL_PATH);
            remove(configPath.c_str());
            rename(MODEL_PATH_PREV, MODEL_PATH);
            rename(CONFIG_PATH_PREV, configPath.c_str());
        }
        return;
    }
    tFilesOk = markEvent("files_ok");

    reconfigureRuntime();
    if (lastApplyOk) return;

    // rollback para a configuracao anterior
    ESP_LOGW(TAG, "Falha ao aplicar a nova configuracao: voltando a anterior");
    summaryPending = false;
    if (!hadActive)
    {
        publishStatus("stopped", "falha ao aplicar e nao havia configuracao anterior");
        return;
    }
    remove(MODEL_PATH);
    remove(configPath.c_str());
    rename(MODEL_PATH_PREV, MODEL_PATH);
    rename(CONFIG_PATH_PREV, configPath.c_str());
    reconfigureRuntime();
    summaryPending = false;
    publishStatus(lastApplyOk ? "rollback" : "stopped", "falha ao aplicar a nova configuracao");
}

// ---------------------------------------------------------------------------
// Laco principal
// ---------------------------------------------------------------------------

void SystemController::pollTestTrigger()
{
    // Gatilho local de teste: botao BOOT (GPIO 0). Alterna entre test_a.json e test_b.json.
    static int lastLevel = 1;
    static int next = 0;
    static int64_t lastPressUs = 0;

    const int level = gpio_get_level(GPIO_NUM_0);
    if (lastLevel == 1 && level == 0 && (esp_timer_get_time() - lastPressUs) > 500000)
    {
        lastPressUs = esp_timer_get_time();
        const char* path = TEST_PAYLOAD[next];
        next ^= 1;

        const uint32_t t0 = nowUs();
        std::string text;
        if (!readFile(path, text))
        {
            ESP_LOGE(TAG, "Nao consegui ler %s", path);
        }
        else
        {
            ESP_LOGI(TAG, "Gatilho de teste: aplicando %s", path);
            systemSubmitPayload(text.data(), text.size(), t0, nowUs());
        }
    }
    lastLevel = level;
}

void SystemController::pollSummary()
{
    const uint32_t tInf = tFirstInference.load();
    if (!summaryPending || tInf == 0) return;
    summaryPending = false;
    if (tFilesOk == 0) return;                // configuracao de boot: sem tempos de recepcao

    const uint32_t tSamp = tFirstSample.load();
    const float transfer = (tRxDone - tRxFirst) / 1000.0f;
    const float persist  = (tFilesOk - tRxDone) / 1000.0f;
    const float swtch    = (tSelfTest - tFilesOk) / 1000.0f;
    const float refill   = (tInf - tSamp) / 1000.0f;
    const float reconf   = (tInf - tRxDone) / 1000.0f;
    const float gap      = (tInf - tLastOld) / 1000.0f;

    ESP_LOGI(TAG_EVT,
             "summary trial=%d tag=%s version=%d transfer_ms=%.1f persist_ms=%.1f switch_ms=%.1f "
             "refill_ms=%.1f reconf_ms=%.1f gap_ms=%.1f",
             trialId, trialTag.c_str(), cfg.version, transfer, persist, swtch, refill, reconf, gap);

    char js[320];
    snprintf(js, sizeof(js),
             "{\"state\":\"summary\",\"trial\":%d,\"tag\":\"%s\",\"version\":%d,\"transfer_ms\":%.1f,"
             "\"persist_ms\":%.1f,\"switch_ms\":%.1f,\"refill_ms\":%.1f,\"reconf_ms\":%.1f,\"gap_ms\":%.1f}",
             trialId, trialTag.c_str(), cfg.version, transfer, persist, swtch, refill, reconf, gap);
    if (g_statusSink) g_statusSink(js);
}

void SystemController::mainLoop()
{
    g_controller = this;

    frameQueue = xQueueCreate(128, sizeof(Frame));
    inbox = xQueueCreate(2, sizeof(Pending*));
    parkEvents = xEventGroupCreate();
    if (!frameQueue || !inbox || !parkEvents)
    {
        ESP_LOGE(TAG, "Sem memoria para filas e eventos");
        vTaskDelete(nullptr);
        return;
    }

    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);

    // as tarefas nascem paradas (pauseReq = true) ate haver uma configuracao valida
    xTaskCreatePinnedToCore(acqEntry, "acquisition", 4096, this, 5, nullptr, 1);
    xTaskCreatePinnedToCore(infEntry, "inference", 16384, this, 4, nullptr, 0);

    if (fileExists(configPath.c_str()) && fileExists(MODEL_PATH))
    {
        reconfigureRuntime();
        if (!lastApplyOk)
        {
            ESP_LOGE(TAG, "Configuracao de boot invalida: sistema parado ate receber uma nova");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Sem configuracao em %s: aguardando um payload", configPath.c_str());
    }

    for (;;)
    {
        Pending* p = nullptr;
        if (xQueueReceive(inbox, &p, pdMS_TO_TICKS(100)) == pdTRUE && p)
        {
            tRxFirst = p->rxFirstUs;
            tRxDone = p->rxDoneUs;

            JsonDocument doc;
            const DeserializationError e = deserializeJson(doc, p->json);
            if (e)
            {
                publishStatus("rejected", e.c_str());
            }
            else
            {
                onMqttConfigReceived(doc);
            }
            delete p;
        }

        pollTestTrigger();
        pollSummary();
    }
}
