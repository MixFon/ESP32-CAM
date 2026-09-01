#pragma once

#include "esp_err.h"

// Инициализирует камеру (OV2640, AI-Thinker пины). Возвращает ESP_OK при успехе.
esp_err_t camera_start(void);
