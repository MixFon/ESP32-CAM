#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Поднимает TLS+MQTT подключение к брокеру (CA, логин/пароль, Last Will
// home/{room}/status=offline). Публикует home/{room}/status=online при
// каждом (пере)подключении. Реконнект при обрыве — встроен в esp-mqtt.
void mqtt_start(void);

// true, пока есть активное подключение к брокеру.
bool mqtt_is_connected(void);

// Публикует home/{room}/<metric> = value (одна цифра после запятой), QoS 1.
void mqtt_publish_metric(const char *metric, float value);

// Публикует бинарные данные (например, JPEG-кадр) в home/{room}/<subtopic>,
// QoS 0, без retain. Блокирует вызывающую задачу на время отправки.
// Возвращает true при успехе.
bool mqtt_publish_binary(const char *subtopic, const uint8_t *data, size_t len);
