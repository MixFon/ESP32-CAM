#pragma once

// Поднимает TLS+MQTT подключение к брокеру (CA, логин/пароль, Last Will
// home/{room}/status=offline). Публикует home/{room}/status=online при
// каждом (пере)подключении. Реконнект при обрыве — встроен в esp-mqtt.
void mqtt_start(void);

// Публикует home/{room}/<metric> = value (одна цифра после запятой), QoS 1.
void mqtt_publish_metric(const char *metric, float value);
