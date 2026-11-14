#include "net_portal.h"
#include "netcfg.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

namespace
{
    constexpr char TAG[]     = "PORTAL";
    constexpr char AP_SSID[] = "TCC-Motor-Setup";
    constexpr char AP_PASS[] = "tccsetup1";

    volatile bool active = false;
    httpd_handle_t server = nullptr;

    const char PAGE[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>TCC Motor</title></head>"
        "<body style='font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em'>"
        "<h2>Configurar rede (2,4 GHz)</h2>"
        "<form method='POST' action='/save'>"
        "<p>Rede (SSID)<br><input name='ssid' maxlength='32' style='width:100%' required></p>"
        "<p>Senha<br><input name='pass' type='password' maxlength='63' style='width:100%'></p>"
        "<p>Broker MQTT<br><input name='mqtt' maxlength='95' placeholder='mqtt://192.168.0.10:1883' "
        "style='width:100%' required></p>"
        "<p><button type='submit'>Salvar e reiniciar</button></p>"
        "</form></body></html>";

    const char PAGE_OK[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
        "<body style='font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em'>"
        "<h2>Salvo.</h2><p>O dispositivo vai reiniciar e conectar na rede informada.</p></body></html>";

    void restartTask(void*)
    {
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
    }

    // Decodifica application/x-www-form-urlencoded no proprio buffer
    void urlDecode(char* s)
    {
        char* o = s;
        while (*s)
        {
            if (*s == '+')
            {
                *o++ = ' ';
                s++;
            }
            else if (*s == '%' && s[1] && s[2])
            {
                const char hex[3] = { s[1], s[2], 0 };
                *o++ = static_cast<char>(strtol(hex, nullptr, 16));
                s += 3;
            }
            else
            {
                *o++ = *s++;
            }
        }
        *o = '\0';
    }

    bool getField(const char* body, const char* key, char* out, size_t outSize)
    {
        char tmp[200];
        if (httpd_query_key_value(body, key, tmp, sizeof(tmp)) != ESP_OK) return false;
        urlDecode(tmp);
        strncpy(out, tmp, outSize - 1);
        out[outSize - 1] = '\0';
        return true;
    }

    esp_err_t onRoot(httpd_req_t* req)
    {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
    }

    esp_err_t onSave(httpd_req_t* req)
    {
        char body[400];
        if (req->content_len == 0 || req->content_len >= sizeof(body))
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Dados invalidos");
            return ESP_FAIL;
        }

        size_t got = 0;
        while (got < req->content_len)
        {
            const int r = httpd_req_recv(req, body + got, req->content_len - got);
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            if (r <= 0)
            {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha na leitura");
                return ESP_FAIL;
            }
            got += static_cast<size_t>(r);
        }
        body[got] = '\0';

        NetConfig c = {};
        const bool okSsid = getField(body, "ssid", c.ssid, sizeof(c.ssid));
        getField(body, "pass", c.pass, sizeof(c.pass));            // vazio = rede aberta
        const bool okMqtt = getField(body, "mqtt", c.mqttUri, sizeof(c.mqttUri));

        if (!okSsid || c.ssid[0] == '\0' || !okMqtt || strncmp(c.mqttUri, "mqtt", 4) != 0)
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID ou endereco do broker invalido");
            return ESP_FAIL;
        }
        if (!netcfgSave(c))
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao gravar");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Rede salva: \"%s\" | broker %s", c.ssid, c.mqttUri);
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_send(req, PAGE_OK, HTTPD_RESP_USE_STRLEN);
        xTaskCreate(restartTask, "restart", 2048, nullptr, 3, nullptr);
        return ESP_OK;
    }

    // Roda numa tarefa propria: a pilha da tarefa de eventos e pequena para isso
    void portalTask(void*)
    {
        esp_netif_create_default_wifi_ap();

        wifi_config_t ap = {};
        strncpy(reinterpret_cast<char*>(ap.ap.ssid), AP_SSID, sizeof(ap.ap.ssid) - 1);
        ap.ap.ssid_len = sizeof(AP_SSID) - 1;
        strncpy(reinterpret_cast<char*>(ap.ap.password), AP_PASS, sizeof(ap.ap.password) - 1);
        ap.ap.channel = 1;
        ap.ap.max_connection = 2;
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_set_config(WIFI_IF_AP, &ap);

        httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
        hc.stack_size = 6144;
        if (httpd_start(&server, &hc) == ESP_OK)
        {
            httpd_uri_t root = {};
            root.uri = "/";
            root.method = HTTP_GET;
            root.handler = onRoot;
            httpd_register_uri_handler(server, &root);

            httpd_uri_t save = {};
            save.uri = "/save";
            save.method = HTTP_POST;
            save.handler = onSave;
            httpd_register_uri_handler(server, &save);
        }
        else
        {
            ESP_LOGE(TAG, "Falha ao iniciar o servidor HTTP");
        }

        ESP_LOGW(TAG, "Portal ativo: conecte em \"%s\" (senha %s) e abra http://192.168.4.1",
                 AP_SSID, AP_PASS);
        vTaskDelete(nullptr);
    }
}

void portalStart()
{
    if (active) return;
    active = true;
    xTaskCreate(portalTask, "portal", 6144, nullptr, 3, nullptr);
}

bool portalIsActive()
{
    return active;
}
