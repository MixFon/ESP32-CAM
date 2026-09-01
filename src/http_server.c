#include "http_server.h"

#include <stdio.h>
#include <string.h>

#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "http_server";
static const char *STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t index_handler(httpd_req_t *req)
{
    static const char html[] =
        "<!DOCTYPE html><html><body>"
        "<h1>ESP32-CAM test</h1>"
        "<p><a href=\"/capture\">Single capture</a></p>"
        "<img src=\"/stream\" style=\"max-width:100%\">"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t err = httpd_resp_send(req, (const char *) fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return err;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t err = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    if (err != ESP_OK) {
        return err;
    }

    char part_buf[64];
    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            err = ESP_FAIL;
            break;
        }

        err = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (err == ESP_OK) {
            size_t header_len = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
            err = httpd_resp_send_chunk(req, part_buf, header_len);
        }
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(req, (const char *) fb->buf, fb->len);
        }

        esp_camera_fb_return(fb);

        if (err != ESP_OK) {
            break;
        }
    }

    return err;
}

void http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    httpd_uri_t capture_uri = {.uri = "/capture", .method = HTTP_GET, .handler = capture_handler};
    httpd_uri_t stream_uri = {.uri = "/stream", .method = HTTP_GET, .handler = stream_handler};

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &capture_uri);
    httpd_register_uri_handler(server, &stream_uri);

    ESP_LOGI(TAG, "HTTP server started");
}
