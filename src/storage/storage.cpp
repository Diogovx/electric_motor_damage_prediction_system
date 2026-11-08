#include "storage.h"

#include "esp_littlefs.h"
#include "esp_log.h"

static const char* TAG = "STORAGE";

esp_err_t storageInit()
{
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = STORAGE_BASE_PATH;
    conf.partition_label = "storage";
    conf.format_if_mount_failed = true;
    conf.dont_mount = false;

    const esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao montar o LittleFS: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    if (esp_littlefs_info(conf.partition_label, &total, &used) == ESP_OK)
    {
        ESP_LOGI(TAG, "LittleFS montado em %s: %u de %u bytes usados",
                 STORAGE_BASE_PATH, (unsigned)used, (unsigned)total);
    }
    return ESP_OK;
}
