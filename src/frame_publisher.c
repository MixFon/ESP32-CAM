#include "frame_publisher.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt.h"

// Интервалы можно переопределить через build_flags, например
// -DFRAME_INTERVAL_MS=500 -DIDLE_FRAME_INTERVAL_MS=0
#ifndef FRAME_INTERVAL_MS
#define FRAME_INTERVAL_MS 333            // есть зритель: ~3 кадра/с
#endif
#ifndef IDLE_FRAME_INTERVAL_MS
#define IDLE_FRAME_INTERVAL_MS 60000     // нет зрителей: 1 кадр/мин; 0 — не слать вовсе
#endif

// Страница шлёт пинг каждые 10 с, пока открыта и видна. Если пингов нет
// дольше VIEWER_TIMEOUT_MS — зрителей нет (запас на 2 потерянных пинга).
#define VIEWER_TIMEOUT_MS 30000

// Кадры — вне ветки home/: на home/+/... подписан iot-backend (графики
// датчиков), бинарный поток ему не нужен. Статус устройства остаётся
// в home/{room}/status.
#define FRAME_TOPIC "spider/cam/frame"
#define VIEWER_TOPIC "spider/cam/viewer"

#define STATS_PERIOD_US (10 * 1000 * 1000)
// TLS-шифрование (mbedtls) выполняется в контексте вызывающей задачи,
// поэтому стека нужно заметно больше минимального.
#define TASK_STACK_SIZE 6144

static const char *TAG = "frame_pub";

static TaskHandle_t task_handle;
// Тик последнего пинга зрителя. TickType_t — 32 бита, запись атомарна.
static volatile TickType_t last_viewer_tick;
static volatile bool have_viewer_ping;

// Счётчики для телеметрии: копятся здесь, забираются и обнуляются из задачи
// telemetry (frame_publisher_take_stats), поэтому под спинлоком.
static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static frame_stats_t tele_stats;
static volatile bool live_mode;

void frame_publisher_take_stats(frame_stats_t *out)
{
    taskENTER_CRITICAL(&stats_lock);
    *out = tele_stats;
    tele_stats = (frame_stats_t) {0};
    taskEXIT_CRITICAL(&stats_lock);
}

bool frame_publisher_is_live(void)
{
    return live_mode;
}

static void count_frame(bool ok, size_t len)
{
    taskENTER_CRITICAL(&stats_lock);
    if (ok) {
        tele_stats.frames++;
        tele_stats.bytes += len;
    } else {
        tele_stats.failures++;
    }
    taskEXIT_CRITICAL(&stats_lock);
}

static bool viewer_present(void)
{
    return have_viewer_ping &&
           (xTaskGetTickCount() - last_viewer_tick) < pdMS_TO_TICKS(VIEWER_TIMEOUT_MS);
}

// Вызывается в задаче esp-mqtt — только отметить время и разбудить задачу.
static void on_viewer_ping(const char *data, int len)
{
    bool was_present = viewer_present();
    last_viewer_tick = xTaskGetTickCount();
    have_viewer_ping = true;
    if (!was_present && task_handle) {
        // Из ожидания (до минуты) — сразу в поток, без задержки.
        xTaskNotifyGive(task_handle);
    }
}

static void frame_publisher_task(void *arg)
{
    int64_t stats_start = esp_timer_get_time();
    uint32_t frames = 0, failures = 0;
    uint64_t bytes = 0, publish_us = 0;
    bool live = false;

    while (true) {
        TickType_t cycle_start = xTaskGetTickCount();

        bool now_live = viewer_present();
        if (now_live != live) {
            live = now_live;
            live_mode = live;
            // Смена режима — сразу на график, не дожидаясь минутной телеметрии.
            if (mqtt_is_connected()) {
                mqtt_publish_metric("mode", live ? 1.0f : 0.0f);
            }
            // ESP_LOGx склеивает формат с префиксом — нужен строковый литерал.
            if (live) {
                ESP_LOGI(TAG, "Появился зритель — поток, кадр раз в %d мс", FRAME_INTERVAL_MS);
            } else {
                ESP_LOGI(TAG, "Зрителей нет — режим ожидания, кадр раз в %d мс", IDLE_FRAME_INTERVAL_MS);
            }
        }

        bool should_send = mqtt_is_connected() && (live || IDLE_FRAME_INTERVAL_MS > 0);
        if (should_send) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (fb) {
                int64_t t0 = esp_timer_get_time();
                bool ok = mqtt_publish_binary(FRAME_TOPIC, fb->buf, fb->len);
                int64_t t1 = esp_timer_get_time();
                size_t len = fb->len;
                esp_camera_fb_return(fb);
                count_frame(ok, len);

                if (ok) {
                    frames++;
                    bytes += len;
                    publish_us += (uint64_t) (t1 - t0);
                } else {
                    failures++;
                }
            } else {
                ESP_LOGW(TAG, "Не удалось получить кадр с камеры");
                count_frame(false, 0);
                failures++;
            }
        }

        // Статистика — только пока идёт поток, чтобы не засорять лог в ожидании.
        int64_t now_us = esp_timer_get_time();
        if (now_us - stats_start >= STATS_PERIOD_US) {
            if (live) {
                float secs = (float) (now_us - stats_start) / 1e6f;
                ESP_LOGI(TAG, "%.1f кадр/с, средний кадр %u Б, отправка %u мс, ошибок %u",
                         frames / secs,
                         frames ? (unsigned) (bytes / frames) : 0,
                         frames ? (unsigned) (publish_us / frames / 1000) : 0,
                         (unsigned) failures);
            }
            stats_start = now_us;
            frames = failures = 0;
            bytes = publish_us = 0;
        }

        // Ждём до следующего кадра. Без связи с брокером проверяем раз в
        // полсекунды. Пинг первого зрителя прерывает ожидание сразу.
        TickType_t interval;
        if (!mqtt_is_connected()) {
            interval = pdMS_TO_TICKS(500);
        } else if (live) {
            interval = pdMS_TO_TICKS(FRAME_INTERVAL_MS);
        } else if (IDLE_FRAME_INTERVAL_MS > 0) {
            interval = pdMS_TO_TICKS(IDLE_FRAME_INTERVAL_MS);
        } else {
            interval = pdMS_TO_TICKS(VIEWER_TIMEOUT_MS);
        }
        TickType_t elapsed = xTaskGetTickCount() - cycle_start;
        TickType_t wait = elapsed < interval ? interval - elapsed : 0;
        ulTaskNotifyTake(pdTRUE, wait);
    }
}

void frame_publisher_start(void)
{
    xTaskCreate(frame_publisher_task, "frame_pub", TASK_STACK_SIZE, NULL, 5, &task_handle);
    mqtt_subscribe(VIEWER_TOPIC, on_viewer_ping);
    ESP_LOGI(TAG, "Публикация кадров: поток %d мс при зрителе, %d мс без зрителей",
             FRAME_INTERVAL_MS, IDLE_FRAME_INTERVAL_MS);
}
