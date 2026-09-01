#pragma once

// Запускает HTTP-сервер: "/" — страница с MJPEG-превью, "/capture" — один
// JPEG-кадр, "/stream" — MJPEG-поток.
void http_server_start(void);
