#include "mqtt.h"

#include <stdio.h>
#include <string.h>

#include "ca_cert_pem.h"
#include "esp_log.h"
#include "mqtt_client.h"

#ifndef MQTT_HOST
#error "MQTT_HOST не задан — экспортируй переменные окружения перед сборкой"
#endif
#ifndef MQTT_PORT
#error "MQTT_PORT не задан — экспортируй переменные окружения перед сборкой"
#endif
#ifndef MQTT_USER
#error "MQTT_USER не задан — экспортируй переменные окружения перед сборкой"
#endif
#ifndef MQTT_PASS
#error "MQTT_PASS не задан — экспортируй переменные окружения перед сборкой"
#endif
#ifndef DEVICE_ROOM
#error "DEVICE_ROOM не задан — экспортируй переменные окружения перед сборкой"
#endif

#define STATUS_TOPIC "home/" DEVICE_ROOM "/status"
#define CLIENT_ID "esp32-" DEVICE_ROOM

#define MAX_SUBSCRIPTIONS 4

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t client;
static volatile bool connected;

typedef struct {
    const char *topic;
    mqtt_message_cb_t cb;
} subscription_t;

// Заполняется до/после старта из app_main; читается в задаче esp-mqtt.
// Запись — только добавление в конец с последующим ++count, поэтому
// читатель никогда не видит недописанный элемент.
static subscription_t subscriptions[MAX_SUBSCRIPTIONS];
static volatile int subscription_count;

static void dispatch_message(esp_mqtt_event_handle_t event)
{
    // Входящие сообщения у нас крошечные; куски длинных сообщений
    // (current_data_offset > 0) не нужны.
    if (event->current_data_offset != 0) {
        return;
    }
    for (int i = 0; i < subscription_count; i++) {
        const char *topic = subscriptions[i].topic;
        if ((int) strlen(topic) == event->topic_len &&
            memcmp(topic, event->topic, event->topic_len) == 0) {
            subscriptions[i].cb(event->data, event->data_len);
        }
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Подключились к MQTT-брокеру");
        esp_mqtt_client_publish(event->client, STATUS_TOPIC, "online", 0, 1, 1);
        // Сессия чистая — подписки после каждого (пере)подключения заново.
        for (int i = 0; i < subscription_count; i++) {
            esp_mqtt_client_subscribe(event->client, subscriptions[i].topic, 0);
        }
        connected = true;
        break;
    case MQTT_EVENT_DATA:
        dispatch_message(event);
        break;
    case MQTT_EVENT_DISCONNECTED:
        connected = false;
        ESP_LOGW(TAG, "Отключились от MQTT-брокера, esp-mqtt переподключится сам");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Ошибка MQTT (тип %d)", event->error_handle->error_type);
        break;
    default:
        break;
    }
}

void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = MQTT_HOST,
        .broker.address.port = MQTT_PORT,
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.verification.certificate = CA_CERT_PEM,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
        .credentials.client_id = CLIENT_ID,
        .session.last_will.topic = STATUS_TOPIC,
        .session.last_will.msg = "offline",
        .session.last_will.msg_len = 0,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

bool mqtt_is_connected(void)
{
    return connected;
}

bool mqtt_subscribe(const char *topic, mqtt_message_cb_t cb)
{
    if (subscription_count >= MAX_SUBSCRIPTIONS) {
        ESP_LOGE(TAG, "Слишком много подписок, %s не добавлена", topic);
        return false;
    }
    subscriptions[subscription_count] = (subscription_t) {.topic = topic, .cb = cb};
    subscription_count++;

    // Если подключение уже есть, событие CONNECTED эту подписку не увидит.
    if (connected) {
        esp_mqtt_client_subscribe(client, topic, 0);
    }
    return true;
}

bool mqtt_publish_binary(const char *topic, const uint8_t *data, size_t len)
{
    if (!connected) {
        return false;
    }

    // QoS 0: кадр не копируется в outbox esp-mqtt (при QoS 1 каждый JPEG
    // дублировался бы в куче до подтверждения), а потерянный кадр не важен —
    // следующий придёт через долю секунды. Сообщения больше выходного буфера
    // esp-mqtt отправляет частями напрямую из data.
    int msg_id = esp_mqtt_client_publish(client, topic, (const char *) data, (int) len, 0, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Не удалось опубликовать %s (%u байт)", topic, (unsigned) len);
        return false;
    }
    return true;
}

void mqtt_publish_metric(const char *metric, float value)
{
    char topic[64];
    char payload[16];
    snprintf(topic, sizeof(topic), "home/%s/%s", DEVICE_ROOM, metric);
    snprintf(payload, sizeof(payload), "%.1f", value);

    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Не удалось опубликовать %s", topic);
    }
}
