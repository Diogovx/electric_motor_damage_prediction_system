#include "net_link.h"
#include "secrets.h"
#include "system_controller.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#include <stdlib.h>
#include <string.h>

namespace
{
    constexpr char TAG[] = "NET";
    constexpr const char* TOPIC_CMD    = "tcc/motor/cmd/reconfig";
    constexpr const char* TOPIC_STATUS = "tcc/motor/status";
    constexpr size_t MAX_PAYLOAD = 32 * 1024;

    esp_mqtt_client_handle_t client = nullptr;

    // remontagem de mensagens fragmentadas
    char* rxBuf = nullptr;
    size_t rxTotal = 0;
    uint32_t rxFirstUs = 0;

    void statusSink(const char* json)
    {
        if (client) esp_mqtt_client_publish(client, TOPIC_STATUS, json, 0, 1, 1);
    }

    void dropRx()
    {
        free(rxBuf);
        rxBuf = nullptr;
        rxTotal = 0;
    }

    void mqttHandler(void*, esp_event_base_t, int32_t id, void* data)
    {
        auto* e = static_cast<esp_mqtt_event_handle_t>(data);
        switch (id)
        {
            case MQTT_EVENT_CONNECTED:
                ESP_LOGI(TAG, "MQTT conectado");
                esp_mqtt_client_subscribe(client, TOPIC_CMD, 1);
                esp_mqtt_client_publish(client, TOPIC_STATUS, "{\"state\":\"online\"}", 0, 1, 1);
                break;

            case MQTT_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "MQTT desconectado");
                break;

            case MQTT_EVENT_DATA:
            {
                if (e->current_data_offset == 0)
                {
                    dropRx();
                    rxTotal = static_cast<size_t>(e->total_data_len);
                    rxFirstUs = static_cast<uint32_t>(esp_timer_get_time());
                    if (rxTotal == 0 || rxTotal > MAX_PAYLOAD)
                    {
                        ESP_LOGE(TAG, "Mensagem com tamanho invalido: %u", static_cast<unsigned>(rxTotal));
                        rxTotal = 0;
                        break;
                    }
                    rxBuf = static_cast<char*>(malloc(rxTotal));
                    if (!rxBuf)
                    {
                        ESP_LOGE(TAG, "Sem memoria para a mensagem");
                        break;
                    }
                }
                if (!rxBuf) break;

                const size_t off = static_cast<size_t>(e->current_data_offset);
                const size_t len = static_cast<size_t>(e->data_len);
                if (off + len > rxTotal)
                {
                    dropRx();
                    break;
                }
                memcpy(rxBuf + off, e->data, len);

                if (off + len == rxTotal)
                {
                    const uint32_t done = static_cast<uint32_t>(esp_timer_get_time());
                    if (!systemSubmitPayload(rxBuf, rxTotal, rxFirstUs, done))
                    {
                        ESP_LOGW(TAG, "Controlador ocupado: mensagem descartada");
                    }
                    dropRx();
                }
                break;
            }

            default:
                break;
        }
    }

    void startMqtt()
    {
        if (client) return;

        esp_mqtt_client_config_t cfg = {};
        cfg.broker.address.uri = MQTT_URI;
        cfg.credentials.client_id = MQTT_CLIENT_ID;
        cfg.session.keepalive = 30;
        cfg.session.last_will.topic = TOPIC_STATUS;
        cfg.session.last_will.msg = "{\"state\":\"offline\"}";
        cfg.session.last_will.qos = 1;
        cfg.session.last_will.retain = 1;
        cfg.buffer.size = 4096;
        cfg.buffer.out_size = 1024;
        cfg.network.reconnect_timeout_ms = 3000;

        client = esp_mqtt_client_init(&cfg);
        esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqttHandler, nullptr);
        esp_mqtt_client_start(client);
        systemSetStatusSink(statusSink);
    }

    void wifiHandler(void*, esp_event_base_t base, int32_t id, void* data)
    {
        if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
        {
            esp_wifi_connect();
        }
        else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
        {
            ESP_LOGW(TAG, "Wi-Fi desconectado: tentando de novo");
            esp_wifi_connect();
        }
        else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        {
            auto* ev = static_cast<ip_event_got_ip_t*>(data);
            ESP_LOGI(TAG, "IP obtido: " IPSTR, IP2STR(&ev->ip_info.ip));
            startMqtt();
        }
    }
}

void netLinkStart()
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        r = nvs_flash_init();
    }
    if (r != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS: %s", esp_err_to_name(r));
        return;
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wic = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wic);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiHandler, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifiHandler, nullptr);

    wifi_config_t wc = {};
    strncpy(reinterpret_cast<char*>(wc.sta.ssid), WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy(reinterpret_cast<char*>(wc.sta.password), WIFI_PASS, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
}
