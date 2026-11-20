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

// Обработчик входящего сообщения. Вызывается в задаче esp-mqtt — должен быть
// коротким и не блокировать. data не завершается нулём.
typedef void (*mqtt_message_cb_t)(const char *data, int len);

// Подписывается на topic (QoS 0) и восстанавливает подписку после каждого
// переподключения. topic должен жить всё время работы (строковый литерал).
// Не более 4 подписок. Возвращает false, если место кончилось.
bool mqtt_subscribe(const char *topic, mqtt_message_cb_t cb);

// Публикует home/{room}/<metric> = value (одна цифра после запятой), QoS 1.
void mqtt_publish_metric(const char *metric, float value);

// Публикует бинарные данные (например, JPEG-кадр) в топик topic как есть
// (без префикса home/{room}), QoS 0, без retain. Блокирует вызывающую задачу
// на время отправки. Возвращает true при успехе.
bool mqtt_publish_binary(const char *topic, const uint8_t *data, size_t len);
