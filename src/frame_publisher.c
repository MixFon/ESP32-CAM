#include "frame_publisher.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt.h"

// Можно переопределить через build_flags: -DFRAME_INTERVAL_MS=500
#ifndef FRAME_INTERVAL_MS
#define FRAME_INTERVAL_MS 333
#endif

#define FRAME_SUBTOPIC "cam/frame"
#define STATS_PERIOD_US (10 * 1000 * 1000)
// TLS-шифрование (mbedtls) выполняется в контексте вызывающей задачи,
// поэтому стека нужно заметно больше минимального.
#define TASK_STACK_SIZE 6144

static const char *TAG = "frame_pub";

static void frame_publisher_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    int64_t stats_start = esp_timer_get_time();
    uint32_t frames = 0, failures = 0;
    uint64_t bytes = 0, publish_us = 0;

    while (true) {
        if (!mqtt_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(500));
            last_wake = xTaskGetTickCount();
            continue;
        }

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "Не удалось получить кадр с камеры");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int64_t t0 = esp_timer_get_time();
        bool ok = mqtt_publish_binary(FRAME_SUBTOPIC, fb->buf, fb->len);
        int64_t t1 = esp_timer_get_time();
        size_t len = fb->len;
        esp_camera_fb_return(fb);

        if (ok) {
            frames++;
            bytes += len;
            publish_us += (uint64_t) (t1 - t0);
        } else {
            failures++;
        }

        if (t1 - stats_start >= STATS_PERIOD_US) {
            float secs = (float) (t1 - stats_start) / 1e6f;
            ESP_LOGI(TAG, "%.1f кадр/с, средний кадр %u Б, отправка %u мс, ошибок %u",
                     frames / secs,
                     frames ? (unsigned) (bytes / frames) : 0,
                     frames ? (unsigned) (publish_us / frames / 1000) : 0,
                     (unsigned) failures);
            stats_start = t1;
            frames = failures = 0;
            bytes = publish_us = 0;
        }

        // Если отправка заняла дольше интервала, задержки не будет —
        // просто идём за следующим кадром.
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FRAME_INTERVAL_MS));
    }
}

void frame_publisher_start(void)
{
    xTaskCreate(frame_publisher_task, "frame_pub", TASK_STACK_SIZE, NULL, 5, NULL);
    ESP_LOGI(TAG, "Публикация кадров запущена, интервал %d мс", FRAME_INTERVAL_MS);
}
