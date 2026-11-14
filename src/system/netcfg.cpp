#include "netcfg.h"
#include "secrets.h"

#include "esp_log.h"
#include "nvs.h"

#include <string.h>

namespace
{
    constexpr char TAG[] = "NETCFG";
    constexpr char NS[]  = "netcfg";
}

bool netcfgLoad(NetConfig& cfg)
{
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.ssid, WIFI_SSID, sizeof(cfg.ssid) - 1);
    strncpy(cfg.pass, WIFI_PASS, sizeof(cfg.pass) - 1);
    strncpy(cfg.mqttUri, MQTT_URI, sizeof(cfg.mqttUri) - 1);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;

    auto get = [&](const char* key, char* out, size_t size)
    {
        size_t n = size;
        return nvs_get_str(h, key, out, &n) == ESP_OK;
    };

    NetConfig tmp = {};
    const bool ok = get("ssid", tmp.ssid, sizeof(tmp.ssid)) &&
                    get("pass", tmp.pass, sizeof(tmp.pass)) &&
                    get("mqtt", tmp.mqttUri, sizeof(tmp.mqttUri));
    nvs_close(h);

    if (!ok || tmp.ssid[0] == '\0') return false;
    cfg = tmp;
    return true;
}

bool netcfgSave(const NetConfig& cfg)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;

    const bool ok = nvs_set_str(h, "ssid", cfg.ssid) == ESP_OK &&
                    nvs_set_str(h, "pass", cfg.pass) == ESP_OK &&
                    nvs_set_str(h, "mqtt", cfg.mqttUri) == ESP_OK &&
                    nvs_commit(h) == ESP_OK;
    nvs_close(h);

    if (!ok) ESP_LOGE(TAG, "Falha ao gravar as credenciais no NVS");
    return ok;
}
