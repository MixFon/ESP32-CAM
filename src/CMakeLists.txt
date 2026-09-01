idf_component_register(SRCS "main.c" "wifi.c" "time_sync.c" "mqtt.c" "camera.c" "http_server.c"
                        INCLUDE_DIRS "." "../include"
                        REQUIRES esp_wifi esp_event nvs_flash esp_netif esp_timer esp_psram
                                 mqtt ca_cert esp32-camera esp_http_server)
