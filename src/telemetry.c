#include "telemetry.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "frame_publisher.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt.h"

#ifndef TELEMETRY_INTERVAL_MS
#define TELEMETRY_INTERVAL_MS 60000
#endif

// mqtt_publish_metric -> esp_mqtt_client_publish по TLS: mbedtls в контексте
// вызывающей задачи, стек как у frame_publisher.
#define TASK_STACK_SIZE 6144

static const char *TAG = "telemetry";

static void telemetry_task(void *arg)
{
    int64_t period_start = esp_timer_get_time();
    // Счётчики, накопленные до первого периода, не учитываем.
    frame_stats_t stats;
    frame_publisher_take_stats(&stats);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS));

        int64_t now = esp_timer_get_time();
        float secs = (float) (now - period_start) / 1e6f;
        period_start = now;
        frame_publisher_take_stats(&stats);

        // Без связи не копим: QoS 1 положил бы устаревшие точки в outbox.
        if (!mqtt_is_connected()) {
            continue;
        }

        bool live = frame_publisher_is_live();
        float fps = secs > 0 ? stats.frames / secs : 0;
        float frame_kb = stats.frames ? (float) stats.bytes / stats.frames / 1024.0f : 0;

        mqtt_publish_metric("mode", live ? 1.0f : 0.0f);
        mqtt_publish_metric("fps", fps);
        // Нет кадров за период (например, IDLE_FRAME_INTERVAL_MS=0) — размер
        // не публикуем, чтобы на графике не было ложных нулей.
        if (stats.frames) {
            mqtt_publish_metric("frame_kb", frame_kb);
        }
        mqtt_publish_metric("frame_err", (float) stats.failures);

        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            mqtt_publish_metric("rssi", (float) ap.rssi);
        }

        size_t heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        mqtt_publish_metric("heap_kb", (float) heap / 1024.0f);
        mqtt_publish_metric("uptime_h", (float) now / 3.6e9f);

        ESP_LOGI(TAG, "mode=%d fps=%.1f frame=%.1fКБ err=%u heap=%uКБ",
                 live, fps, frame_kb, (unsigned) stats.failures, (unsigned) (heap / 1024));
    }
}

void telemetry_start(void)
{
    xTaskCreate(telemetry_task, "telemetry", TASK_STACK_SIZE, NULL, 4, NULL);
    ESP_LOGI(TAG, "Телеметрия раз в %d мс в home/%s/<metric>", TELEMETRY_INTERVAL_MS, DEVICE_ROOM);
}
