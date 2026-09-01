## Что сделать самостоятельно перед прошивкой

Проект собирается (`pio run` проходит успешно, framework = espidf), WiFi/TLS/MQTT
код перенесён без изменений из `~/MyFiles/Microcontrollers/ESP32` (тот же брокер
`mrmixfon.ru:8883`, тот же CA). Сейчас в `secrets.sh` — тестовые плейсхолдеры,
с ними на реальную сеть и брокер устройство не подключится.

1. **Заполнить `secrets.sh`** (создан из `secrets.sh.example`, в `.gitignore`):
   - `WIFI_SSID`, `WIFI_PASS` — от вашей точки доступа
   - `MQTT_USER`, `MQTT_PASS` — те же, что в `secrets.sh` соседнего проекта ESP32
   - `MQTT_HOST`, `MQTT_PORT` — менять не нужно (`mrmixfon.ru`, `8883`)
   - `DEVICE_ROOM` — уникальное имя для этого устройства (например
     `litterbox-cam`), чтобы не конфликтовать с топиками другого ESP32

2. **`components/ca_cert/ca.crt`** уже скопирован с тем же CA, что и в проекте
   ESP32 (это публичный сертификат, не секрет). Менять не нужно, если брокер
   тот же.

3. **Собрать и прошить** (ESP32-CAM-MB переводит модуль в режим прошивки сам,
   кнопка не нужна):

   ```
   source secrets.sh
   pio run -t upload
   pio device monitor
   ```

   В логе должно быть: инициализация камеры → подключение Wi-Fi → NTP →
   TLS-хендшейк → "Подключились к MQTT-брокеру" → "HTTP server started".

4. **Проверить камеру**: открыть `http://<IP-устройства>/` в браузере —
   должен показаться MJPEG-стрим; `http://<IP-устройства>/capture` отдаёт
   один JPEG-кадр. Оценить качество изображения (это и есть цель Этапа 1
   из `PLAN.md`) — если оно неудовлетворительное, рассмотреть замену камеры.

5. **Проверить MQTT-статус** (с другого компьютера с `mosquitto_sub` и
   настоящим `ca.crt`):

   ```
   mosquitto_sub -h mrmixfon.ru -p 8883 --cafile ca.crt \
     -u <MQTT_USER> -P <MQTT_PASS> -t 'home/#' -v
   ```

   Должно прийти `home/<DEVICE_ROOM>/status online`. Публикации метрик пока
   нет — это будет добавлено на Этапе 3–4 (датчики давления + фото по MQTT).

## Что уже сделано

- Камера (`src/camera.c`) — OV2640, AI-Thinker пины, PSRAM-буферы кадров.
- HTTP-сервер (`src/http_server.c`, `esp_http_server`) — `/`, `/capture`,
  `/stream` для визуальной проверки качества картинки без MQTT.
- Wi-Fi, NTP, TLS+MQTT (`src/wifi.c`, `src/time_sync.c`, `src/mqtt.c`) —
  перенесены как есть из проекта `ESP32` для единообразия и переиспользования
  уже проверенной логики (тот же брокер, топики `home/{room}/...`, Last Will).
- `sdkconfig.defaults`: 4MB flash, PSRAM включён, увеличенная таблица
  разделов (`partitions_singleapp_large.csv`) — камера + mbedTLS + HTTP не
  влезали в стандартный 1MB app-раздел.
