#include "nvs_flash.h"

#include "camera.h"
#include "esp_err.h"
#include "http_server.h"
#include "mqtt.h"
#include "time_sync.h"
#include "wifi.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(camera_start());

    wifi_init_sta();
    time_sync_wait();
    mqtt_start();

    http_server_start();
}
