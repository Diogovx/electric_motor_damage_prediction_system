#pragma once
#include "esp_err.h"

constexpr const char* STORAGE_BASE_PATH = "/storage";

esp_err_t storageInit();
