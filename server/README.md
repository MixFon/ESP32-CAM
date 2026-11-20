# Серверная часть: страница со стримом паука

```
ESP32-CAM ──MQTT/TLS :8883──► Mosquitto ──WS :9001 (localhost)──► Caddy ──HTTPS/WSS──► браузер
             spider/cam/frame                                      /spider/            mqtt.js
```

Браузер не примет собственный CA брокера, поэтому WebSocket идёт через Caddy
с публичным сертификатом сайта.

Поток по требованию: открытая и видимая страница раз в 10 с публикует пинг в
`spider/cam/viewer`. Пока пинги есть (последний — не старше 30 с), камера шлёт
~3 кадра/с, иначе — 1 кадр в минуту. Поэтому `spider-viewer` в ACL может
писать в `spider/cam/viewer`.

| Файл | Куда на VPS |
|---|---|
| `mosquitto/spider-ws.conf` | `/etc/mosquitto/conf.d/spider-ws.conf` |
| `mosquitto/acl` | `/etc/mosquitto/acl` |
| `caddy/spider.caddy` | вставить внутрь блока сайта в `/etc/caddy/Caddyfile` |
| `www/index.html`, `www/config.js` | `/var/www/spider/` |

## Быстрая установка

С рабочего компьютера, из корня проекта (SSH на порту 24789):

```
ssh -p 24789 root@mrmixfon.ru 'rm -rf ~/spider-server'
scp -P 24789 -r server root@mrmixfon.ru:~/spider-server
ssh -t -p 24789 root@mrmixfon.ru 'cd ~/spider-server && bash deploy.sh'
```

`deploy.sh` делает шаги 1–3 ниже: генерирует пароль `spider-viewer`, спрашивает
пароль на страницу, делает резервные копии (`*.bak-<дата>`), проверяет каждый
шаг и откатывает Mosquitto/Caddy при ошибке. Повторный запуск безопасен.

Ниже — то же самое вручную.

## 1. Mosquitto

```
cd ~/spider-server/mosquitto
mosquitto_passwd /etc/mosquitto/passwd spider-viewer   # спросит пароль
cp spider-ws.conf /etc/mosquitto/conf.d/
cp acl /etc/mosquitto/acl
chown root:mosquitto /etc/mosquitto/acl && chmod 640 /etc/mosquitto/acl
systemctl restart mosquitto
journalctl -u mosquitto -n 20
ss -tlnp | grep 9001
```

Сразу после рестарта проверить, что датчики живы:
`journalctl -u iot-backend -n 10` — `livingroom` и `spider-cam` снова `online`.
Если нет — откатить: удалить `/etc/mosquitto/conf.d/spider-ws.conf`,
`systemctl restart mosquitto`.

## 2. Страница

```
mkdir -p /var/www/spider
cp ~/spider-server/www/index.html /var/www/spider/
cp ~/spider-server/www/config.example.js /var/www/spider/config.js
nano /var/www/spider/config.js      # пароль spider-viewer из шага 1
chmod 644 /var/www/spider/*
```

## 3. Caddy

```
caddy hash-password                  # пароль на страницу, выведет хэш
cp /etc/caddy/Caddyfile /etc/caddy/Caddyfile.bak
nano /etc/caddy/Caddyfile
```

Вставить содержимое `caddy/spider.caddy` внутрь блока `mrmixfon.ru { ... }`,
заменить `ВСТАВИТЬ_ХЭШ` на хэш. Затем:

```
caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile
systemctl reload caddy
```

Если что-то сломалось: `cp /etc/caddy/Caddyfile.bak /etc/caddy/Caddyfile && systemctl reload caddy`.

Открыть `https://mrmixfon.ru/spider/` (логин `spider`, пароль из `caddy hash-password`).
