#include "time_sync.h"

#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "time_sync";

void time_sync_wait(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&config));

    ESP_LOGI(TAG, "Ждём синхронизации времени по NTP...");
    esp_err_t err;
    do {
        err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Время ещё не синхронизировано (%s), пробуем ещё раз", esp_err_to_name(err));
        }
    } while (err != ESP_OK);

    time_t now = time(NULL);
    ESP_LOGI(TAG, "Время синхронизировано: %s", ctime(&now));
}
