#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t frames;    // успешно отправленных кадров
    uint32_t failures;  // не получен кадр с камеры или не ушёл в MQTT
    uint64_t bytes;     // суммарный размер отправленных кадров
} frame_stats_t;

// Забирает счётчики, накопленные с прошлого вызова, и обнуляет их.
void frame_publisher_take_stats(frame_stats_t *out);

// true — есть зритель и идёт поток, false — режим ожидания.
bool frame_publisher_is_live(void);

// Запускает фоновую задачу, которая публикует JPEG-кадры с камеры в
// spider/cam/frame, пока есть подключение к MQTT:
//   - есть зритель (пинг в spider/cam/viewer за последние 30 с) — кадр раз в
//     FRAME_INTERVAL_MS (по умолчанию 333 мс, ~3 кадра/с);
//   - зрителей нет — кадр раз в IDLE_FRAME_INTERVAL_MS (по умолчанию раз в
//     минуту; 0 — не слать).
// Первый пинг зрителя сразу будит задачу. Пока идёт поток, раз в 10 секунд
// пишет в лог частоту кадров и средний размер кадра. При смене режима сразу
// публикует home/{room}/mode = 1 (поток) / 0 (ожидание).
void frame_publisher_start(void);
