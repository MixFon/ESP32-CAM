#!/usr/bin/env bash
# Разворачивает страницу со стримом паука на VPS:
#   1) Mosquitto: пользователь spider-viewer, WebSocket-listener 127.0.0.1:9001, ACL;
#   2) страница в /var/www/spider (config.js с паролем spider-viewer);
#   3) Caddy: блок /spider* внутри mrmixfon.ru { ... }.
#
# Запуск на VPS от root из папки, куда скопирован server/:
#   cd ~/spider-server && bash deploy.sh
#
# Можно запускать повторно: пароль spider-viewer обновится, ACL и конфиг
# перезапишутся, блок в Caddyfile второй раз не вставится. Перед каждой
# правкой делается резервная копия с меткой времени; при ошибке Mosquitto
# и Caddy откатываются автоматически.

set -euo pipefail

SITE="mrmixfon.ru"
SRC="$(cd "$(dirname "$0")" && pwd)"
STAMP="$(date +%Y%m%d-%H%M%S)"

MOSQ_CONF_DIR="/etc/mosquitto/conf.d"
MOSQ_WS_CONF="$MOSQ_CONF_DIR/spider-ws.conf"
MOSQ_ACL="/etc/mosquitto/acl"
MOSQ_PASSWD="/etc/mosquitto/passwd"
MOSQ_CA="/etc/mosquitto/certs/ca.crt"
WWW_DIR="/var/www/spider"
CADDYFILE="/etc/caddy/Caddyfile"

VIEWER_USER="spider-viewer"
PAGE_USER="spider"

step() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
ok()   { printf '\033[32m  ✓ %s\033[0m\n' "$*"; }
warn() { printf '\033[33m  ! %s\033[0m\n' "$*"; }
die()  { printf '\033[31m  ✗ %s\033[0m\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- проверки
step "Проверки"
[[ $EUID -eq 0 ]] || die "Запускать от root"
for f in mosquitto/spider-ws.conf mosquitto/acl caddy/spider.caddy www/index.html; do
    [[ -f "$SRC/$f" ]] || die "Нет файла $SRC/$f — скопирована ли папка server целиком?"
done
for c in mosquitto_passwd mosquitto_sub caddy openssl systemctl ss; do
    command -v "$c" >/dev/null || die "Не найдена команда $c"
done
getent group mosquitto >/dev/null || die "Нет группы mosquitto"
[[ -f "$MOSQ_PASSWD" ]] || die "Нет $MOSQ_PASSWD"
[[ -f "$MOSQ_CA" ]] || die "Нет $MOSQ_CA"
[[ -f "$CADDYFILE" ]] || die "Нет $CADDYFILE"
grep -qE "^${SITE//./\\.}[[:space:]]*\{[[:space:]]*$" "$CADDYFILE" \
    || die "В $CADDYFILE не найдена строка '$SITE {'"
ok "Всё на месте"

# ---------------------------------------------------------------- пароли
# Пароль spider-viewer никому не нужно помнить — он только в config.js
# страницы, поэтому генерируется случайно.
VIEWER_PASS="$(openssl rand -base64 24 | tr -d '/+=')"

CADDY_HAS_SPIDER=0
grep -q 'handle /spider\*' "$CADDYFILE" && CADDY_HAS_SPIDER=1

if [[ $CADDY_HAS_SPIDER -eq 0 ]]; then
    step "Пароль для входа на страницу (логин: $PAGE_USER)"
    while true; do
        read -rsp "  Пароль: " PAGE_PASS; echo
        read -rsp "  Ещё раз: " PAGE_PASS2; echo
        [[ -n "$PAGE_PASS" && "$PAGE_PASS" == "$PAGE_PASS2" ]] && break
        warn "Пустой или не совпадает, ещё раз"
    done
    PAGE_HASH="$(caddy hash-password --plaintext "$PAGE_PASS")"
    unset PAGE_PASS PAGE_PASS2
fi

# ---------------------------------------------------------------- Mosquitto
step "Mosquitto"

cp -a "$MOSQ_PASSWD" "$MOSQ_PASSWD.bak-$STAMP"
HAD_ACL=0
if [[ -f "$MOSQ_ACL" ]]; then
    HAD_ACL=1
    cp -a "$MOSQ_ACL" "$MOSQ_ACL.bak-$STAMP"
fi
HAD_WS=0
if [[ -f "$MOSQ_WS_CONF" ]]; then
    HAD_WS=1
    cp -a "$MOSQ_WS_CONF" "$MOSQ_WS_CONF.bak-$STAMP"
fi
ok "Резервные копии: *.bak-$STAMP"

mosquitto_rollback() {
    warn "Откатываю Mosquitto"
    cp -a "$MOSQ_PASSWD.bak-$STAMP" "$MOSQ_PASSWD"
    if [[ $HAD_ACL -eq 1 ]]; then cp -a "$MOSQ_ACL.bak-$STAMP" "$MOSQ_ACL"; else rm -f "$MOSQ_ACL"; fi
    if [[ $HAD_WS -eq 1 ]]; then cp -a "$MOSQ_WS_CONF.bak-$STAMP" "$MOSQ_WS_CONF"; else rm -f "$MOSQ_WS_CONF"; fi
    systemctl restart mosquitto || true
    sleep 2
    systemctl is-active --quiet mosquitto && warn "Mosquitto откачен и работает" \
        || warn "Mosquitto не поднялся даже после отката: journalctl -u mosquitto -n 50"
}

mosquitto_passwd -b "$MOSQ_PASSWD" "$VIEWER_USER" "$VIEWER_PASS"
install -m 644 -o root -g root "$SRC/mosquitto/spider-ws.conf" "$MOSQ_WS_CONF"
install -m 640 -o root -g mosquitto "$SRC/mosquitto/acl" "$MOSQ_ACL"
ok "Пользователь $VIEWER_USER, $MOSQ_WS_CONF, $MOSQ_ACL"

systemctl restart mosquitto
sleep 2
if ! systemctl is-active --quiet mosquitto; then
    journalctl -u mosquitto -n 20 --no-pager || true
    mosquitto_rollback
    die "Mosquitto не запустился с новым конфигом"
fi
# Без grep -q: с pipefail ранний выход grep даёт SIGPIPE у ss и ложную ошибку.
WS_UP=0
for _ in 1 2 3 4 5; do
    if ss -tln | grep '127.0.0.1:9001' >/dev/null; then WS_UP=1; break; fi
    sleep 1
done
[[ $WS_UP -eq 1 ]] || { mosquitto_rollback; die "Порт 9001 не слушается"; }
ok "Mosquitto перезапущен, WebSocket на 127.0.0.1:9001"

# Проверка 1: spider-viewer логинится и читает статус камеры (retained).
if ! mosquitto_sub -h "$SITE" -p 8883 --cafile "$MOSQ_CA" \
        -u "$VIEWER_USER" -P "$VIEWER_PASS" \
        -t 'home/spider-cam/status' -C 1 -W 5 >/dev/null 2>&1; then
    mosquitto_rollback
    die "$VIEWER_USER не смог прочитать home/spider-cam/status (логин или ACL)"
fi
ok "$VIEWER_USER читает статус камеры"

# Проверка 2: кадр доходит — значит, iot (камера) по-прежнему может публиковать.
# После рестарта брокера камере нужно ~10–20 с на переподключение (таймаут
# esp-mqtt + TLS-рукопожатие), поэтому ждём до 40 с.
echo "  Ждём кадр с камеры (до 40 с, она переподключается после рестарта)..."
if mosquitto_sub -h "$SITE" -p 8883 --cafile "$MOSQ_CA" \
        -u "$VIEWER_USER" -P "$VIEWER_PASS" \
        -t 'spider/cam/frame' -C 1 -W 40 >/dev/null 2>&1; then
    ok "Кадры с камеры приходят — права iot на запись в порядке"
else
    warn "За 40 с не пришло ни одного кадра. Если камера включена — проверьте,"
    warn "не сломались ли права iot: journalctl -u iot-backend -n 20"
fi

# ---------------------------------------------------------------- страница
step "Страница $WWW_DIR"
mkdir -p "$WWW_DIR"
chmod 755 "$WWW_DIR"
install -m 644 "$SRC/www/index.html" "$WWW_DIR/index.html"
cat > "$WWW_DIR/config.js" <<EOF
// Сгенерировано deploy.sh $STAMP
window.SPIDER_CONFIG = {
  username: "$VIEWER_USER",
  password: "$VIEWER_PASS",
};
EOF
chmod 644 "$WWW_DIR/config.js"
ok "index.html и config.js на месте"

# ---------------------------------------------------------------- Caddy
step "Caddy"
if [[ $CADDY_HAS_SPIDER -eq 1 ]]; then
    ok "Блок /spider* уже есть в $CADDYFILE — не трогаю"
    if grep -qE 'ВСТАВИТЬ_ХЭШ|ХЭШ_ИЗ_caddy' "$CADDYFILE"; then
        die "В $CADDYFILE остался заполнитель вместо хэша пароля. Удалите блок handle /spider* целиком (скрипт вставит его сам с хэшем) или замените заполнитель на вывод 'caddy hash-password', и запустите снова"
    fi
    if ! caddy validate --config "$CADDYFILE" --adapter caddyfile >/dev/null 2>&1; then
        caddy validate --config "$CADDYFILE" --adapter caddyfile || true
        die "Существующий $CADDYFILE не проходит проверку — reload не делаю, Caddy работает со старым конфигом"
    fi
else
    cp -a "$CADDYFILE" "$CADDYFILE.bak-$STAMP"
    ok "Резервная копия: $CADDYFILE.bak-$STAMP"

    TMP="$(mktemp)"
    # Вставляем содержимое spider.caddy (без шапки-комментария) сразу после
    # строки "mrmixfon.ru {", подставляя хэш пароля.
    awk -v site="$SITE" -v hash="$PAGE_HASH" -v snip="$SRC/caddy/spider.caddy" '
        { print }
        !done {
            line = $0
            sub(/[ \t]*\{[ \t]*$/, "", line)
            if (line == site && $0 ~ /\{[ \t]*$/) {
                while ((getline s < snip) > 0) {
                    if (s ~ /^#/) continue
                    gsub(/ВСТАВИТЬ_ХЭШ/, hash, s)
                    print (s == "" ? "" : "\t" s)
                }
                print ""
                done = 1
            }
        }
    ' "$CADDYFILE" > "$TMP"

    grep -q 'handle /spider\*' "$TMP" || { rm -f "$TMP"; die "Не удалось вставить блок в Caddyfile"; }
    grep -q 'ВСТАВИТЬ_ХЭШ' "$TMP" && { rm -f "$TMP"; die "Хэш пароля не подставился"; }

    # До Caddy 2.8 директива называлась basicauth.
    if caddy version | grep -qE '^v2\.[0-7]\.'; then
        sed -i 's/\bbasic_auth {/basicauth {/' "$TMP"
        warn "Caddy старше 2.8 — использую basicauth"
    fi

    cat "$TMP" > "$CADDYFILE"
    rm -f "$TMP"

    if ! caddy validate --config "$CADDYFILE" --adapter caddyfile >/dev/null 2>&1; then
        caddy validate --config "$CADDYFILE" --adapter caddyfile || true
        cp -a "$CADDYFILE.bak-$STAMP" "$CADDYFILE"
        die "Caddyfile не прошёл проверку — восстановлен из резервной копии"
    fi
    ok "Caddyfile проверен"
fi

if ! systemctl reload caddy; then
    journalctl -u caddy -n 15 --no-pager -o cat || true
    if [[ -f "$CADDYFILE.bak-$STAMP" ]]; then
        cp -a "$CADDYFILE.bak-$STAMP" "$CADDYFILE"
        systemctl reload caddy || true
        die "Caddy не принял новый конфиг — Caddyfile восстановлен из резервной копии"
    fi
    die "Caddy не принял конфиг (лог выше)"
fi
sleep 2
systemctl is-active --quiet caddy || die "Caddy не работает после reload: journalctl -u caddy -n 50"
ok "Caddy перезагружен"

# Проверка: страница закрыта паролем, остальной сайт отвечает как раньше.
if command -v curl >/dev/null; then
    CODE_SPIDER="$(curl -s -o /dev/null -w '%{http_code}' "https://$SITE/spider/" || true)"
    CODE_ROOT="$(curl -s -o /dev/null -w '%{http_code}' "https://$SITE/" || true)"
    [[ "$CODE_SPIDER" == "401" ]] && ok "/spider/ просит пароль (401)" \
        || warn "/spider/ ответил $CODE_SPIDER (ожидался 401)"
    ok "Дашборд / отвечает $CODE_ROOT"
fi

# ---------------------------------------------------------------- итог
step "Готово"
echo "  Страница:  https://$SITE/spider/"
echo "  Логин:     $PAGE_USER  (пароль — тот, что ввели выше)"
echo
echo "  Последние события iot-backend (livingroom и spider-cam должны быть online):"
journalctl -u iot-backend -n 6 --no-pager -o cat 2>/dev/null | sed 's/^/    /' || true
