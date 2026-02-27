#include "http_server.h"
#include "config.h"
#include "tracker.h"
#include "jpeg_stream.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "HTTP";

static httpd_handle_t server = nullptr;

static esp_err_t counts_handler(httpd_req_t *req)
{
    char buf[300];
    xSemaphoreTake(counter_mutex, portMAX_DELAY);
    int len = snprintf(buf, sizeof(buf),
        "{\"big_vehicle\":%lu,\"car\":%lu,\"motorcycle\":%lu,\"total\":%lu"
        ",\"fps\":%.1f,\"free_heap\":%lu,\"tracking\":%d}",
        (unsigned long)vehicle_counts[CLASS_BIG_VEHICLE],
        (unsigned long)vehicle_counts[CLASS_CAR],
        (unsigned long)vehicle_counts[CLASS_MOTORCYCLE],
        (unsigned long)(vehicle_counts[0] + vehicle_counts[1] + vehicle_counts[2]),
        current_fps, (unsigned long)esp_get_free_heap_size(), current_tracking);
    xSemaphoreGive(counter_mutex);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

static esp_err_t reset_handler(httpd_req_t *req)
{
    xSemaphoreTake(counter_mutex, portMAX_DELAY);
    memset(vehicle_counts, 0, sizeof(vehicle_counts));
    xSemaphoreGive(counter_mutex);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><title>Traffic Counter</title>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<link rel='icon' href='data:,'>"
        "<style>"
        "body{font-family:Arial,sans-serif;text-align:center;padding:20px;"
             "background:#0d1117;color:#e6edf3;}"
        ".counter{font-size:52px;margin:8px 0;color:#58a6ff;font-weight:bold;}"
        ".label{font-size:14px;color:#8b949e;text-transform:uppercase;letter-spacing:1px;}"
        "button{padding:10px 22px;font-size:15px;margin:8px;"
               "background:#238636;color:#fff;border:none;border-radius:6px;cursor:pointer;}"
        "button:hover{background:#2ea043;}"
        ".stats{display:flex;justify-content:center;flex-wrap:wrap;gap:16px;margin:20px 0;}"
        ".stat{padding:20px 30px;border:1px solid #30363d;border-radius:10px;"
              "background:#161b22;min-width:130px;}"
        ".info{font-size:13px;color:#8b949e;margin-top:16px;}"
        "h1{color:#58a6ff;margin-bottom:4px;}"
        "p.sub{color:#8b949e;font-size:13px;margin:0 0 16px;}"
        "</style></head><body>"
        "<h1>&#128663; Smart Traffic Counter</h1>"
        "<p class='sub'>ESP32-S3 &mdash; ESPDet-Pico 224&times;224</p>"
        "<div class='stats'>"
        "<div class='stat'><div class='label'>Big Vehicles</div><div class='counter' id='big'>-</div></div>"
        "<div class='stat'><div class='label'>Cars</div><div class='counter' id='car'>-</div></div>"
        "<div class='stat'><div class='label'>Motorcycles</div><div class='counter' id='moto'>-</div></div>"
        "<div class='stat'><div class='label'>Total</div><div class='counter' id='total'>-</div></div>"
        "</div>"
        "<button onclick='resetCounters()'>Reset</button>"
        "<button onclick=\"window.location='/preview'\">Live Preview</button>"
        "<div style='position:relative;display:inline-block;margin:16px 0;'>"
        "<img src='/stream' style='display:block;max-width:480px;width:100%;"
             "border:1px solid #30363d;border-radius:8px;'>"
        "<div style='position:absolute;left:0;right:0;top:50%;height:3px;background:#f44;'>"
        "<span style='position:absolute;right:6px;top:-18px;font-size:11px;color:#f44;"
              "background:rgba(0,0,0,.7);padding:2px 5px;border-radius:3px;'>"
              "&#9660; Count Line</span></div></div>"
        "<div class='info'>"
        "FPS: <span id='fps'>-</span> &nbsp;|&nbsp; "
        "Heap: <span id='heap'>-</span> KB &nbsp;|&nbsp; "
        "Tracking: <span id='tracking'>-</span>"
        "</div>"
        "<script>"
        "function update(){"
        "fetch('/counts').then(r=>r.json()).then(d=>{"
        "document.getElementById('big').textContent=d.big_vehicle;"
        "document.getElementById('car').textContent=d.car;"
        "document.getElementById('moto').textContent=d.motorcycle;"
        "document.getElementById('total').textContent=d.total;"
        "document.getElementById('fps').textContent=d.fps.toFixed(1);"
        "document.getElementById('heap').textContent=(d.free_heap/1024).toFixed(0);"
        "document.getElementById('tracking').textContent=d.tracking;"
        "}).catch(()=>{})};"
        "function resetCounters(){fetch('/reset',{method:'POST'}).then(update);}"
        "update();setInterval(update,2000);"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    uint8_t *local = nullptr; size_t local_len = 0;
    for (int i = 0; i < 20 && !local; i++) {
        if (g_jpg_mutex && xSemaphoreTake(g_jpg_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (g_jpg_buf && g_jpg_len > 0) {
                local = (uint8_t *)malloc(g_jpg_len);
                if (local) { memcpy(local, g_jpg_buf, g_jpg_len); local_len = g_jpg_len; }
            }
            xSemaphoreGive(g_jpg_mutex);
        }
        if (!local) vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!local) { httpd_resp_send_500(req); return ESP_FAIL; }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    esp_err_t res = httpd_resp_send(req, (const char *)local, (ssize_t)local_len);
    free(local);
    return res;
}

#define STREAM_CT   "multipart/x-mixed-replace;boundary=frame"
#define STREAM_BND  "\r\n--frame\r\n"
#define STREAM_PART "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n"

static esp_err_t stream_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, STREAM_CT);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    g_stream_active = true;
    char part_hdr[64];
    esp_err_t res = ESP_OK;
    uint8_t *local = nullptr; size_t local_len = 0;
    while (res == ESP_OK) {
        if (!g_jpg_mutex) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if (xSemaphoreTake(g_jpg_mutex, pdMS_TO_TICKS(600)) == pdTRUE) {
            if (g_jpg_buf && g_jpg_len > 0) {
                if (local_len != g_jpg_len) {
                    if (local) { free(local); local = nullptr; }
                    local = (uint8_t *)malloc(g_jpg_len);
                    local_len = local ? g_jpg_len : 0;
                }
                if (local) memcpy(local, g_jpg_buf, g_jpg_len);
            }
            xSemaphoreGive(g_jpg_mutex);
        }
        if (!local || local_len == 0) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        int hlen = snprintf(part_hdr, sizeof(part_hdr), STREAM_PART, (unsigned)local_len);
        res = httpd_resp_send_chunk(req, STREAM_BND,  strlen(STREAM_BND));
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_hdr, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)local, (ssize_t)local_len);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    if (local) free(local);
    g_stream_active = false;
    return res;
}

static esp_err_t preview_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><title>Preview</title>"
        "<link rel='icon' href='data:,'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:Arial;margin:20px;background:#0d1117;color:#e6edf3;text-align:center;}"
        "img{max-width:100%;border:1px solid #30363d;border-radius:8px;}"
        "button{padding:10px 20px;margin:6px;cursor:pointer;background:#238636;"
               "color:#fff;border:none;border-radius:6px;font-size:15px;}"
        "a{color:#58a6ff;}</style></head><body>"
        "<h2>Live Preview</h2>"
        "<img id='s' src='/stream' onerror=\"this.src='/capture?t='+Date.now()\"><br>"
        "<button onclick=\"document.getElementById('s').src='/stream'\">Resume</button>"
        "<button onclick=\"var s=document.getElementById('s');s.src='';s.src='/capture?t='+Date.now();\">Snapshot</button>"
        "<p><a href='/'>&#8592; Dashboard</a></p></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

void start_webserver(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 7;
    cfg.stack_size       = 8192;
    if (httpd_start(&server, &cfg) != ESP_OK) return;
    httpd_uri_t uris[] = {
        {"/",        HTTP_GET,  root_handler,    nullptr},
        {"/counts",  HTTP_GET,  counts_handler,  nullptr},
        {"/reset",   HTTP_POST, reset_handler,   nullptr},
        {"/capture", HTTP_GET,  capture_handler, nullptr},
        {"/stream",  HTTP_GET,  stream_handler,  nullptr},
        {"/preview", HTTP_GET,  preview_handler, nullptr},
    };
    for (auto &u : uris) httpd_register_uri_handler(server, &u);
    ESP_LOGI(TAG, "HTTP server started");
}
