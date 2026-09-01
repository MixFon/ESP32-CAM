#pragma once

#include "esp_err.h"

// Инициализирует камеру (сенсор определяется автоматически, на этой плате —
// OV3660; AI-Thinker пины). Возвращает ESP_OK при успехе.
esp_err_t camera_start(void);
