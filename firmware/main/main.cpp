#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/image/processing.hpp"

static const char *TAG = "TRAFFIC_COUNTER";

// Camera Pin Configuration
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5
#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13

#define CONFIDENCE_THRESHOLD 0.25f
#define INFERENCE_DELAY_MS   500

// Global State
static httpd_handle_t stream_httpd = NULL;
static uint8_t *g_rgb_buffer = nullptr;
static SemaphoreHandle_t camera_mutex = NULL;

// Vehicle counting
static int car_count = 0;
static int motorcycle_count = 0;
static int big_vehicle_count = 0;

// ============================================================
// HTTP MJPEG STREAMING WITH BOUNDING BOXES
// ============================================================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// Store last detection result for streaming
static ei_impulse_result_t g_last_result = {0};
static SemaphoreHandle_t result_mutex = NULL;

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    while (true) {
        if (!xSemaphoreTake(camera_mutex, pdMS_TO_TICKS(100))) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        fb = esp_camera_fb_get();
        xSemaphoreGive(camera_mutex);
        
        if (!fb) {
            break;
        }

        size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        
        esp_camera_fb_return(fb);
        
        if (res != ESP_OK) {
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return res;
}

static esp_err_t stats_handler(httpd_req_t *req) {
    char response[512];
    snprintf(response, sizeof(response),
        "<!DOCTYPE html><html><body style='font-family:Arial'>"
        "<h1>Traffic Counter</h1>"
        "<h2>Stats</h2>"
        "<p>Cars: <b>%d</b></p>"
        "<p>Motorcycles: <b>%d</b></p>"
        "<p>Big Vehicles: <b>%d</b></p>"
        "<h2>Stream</h2>"
        "<img src='/stream' width='480' height='480' style='border:1px solid black'>"
        "</body></html>",
        car_count, motorcycle_count, big_vehicle_count);
    
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

static void start_stream_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 2;

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
    };

    httpd_uri_t stats_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = stats_handler,
        .user_ctx = NULL
    };

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        httpd_register_uri_handler(stream_httpd, &stats_uri);
    }
}

// ============================================================
// WiFi INITIALIZATION
// ============================================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static bool wait_for_wifi(int timeout_sec) {
    for (int i = 0; i < timeout_sec; i++) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return false;
}

// ============================================================
// CAMERA INITIALIZATION
// ============================================================
static esp_err_t camera_init(void) {
    camera_config_t config = {
        .pin_pwdn = PWDN_GPIO_NUM,
        .pin_reset = RESET_GPIO_NUM,
        .pin_xclk = XCLK_GPIO_NUM,
        .pin_sccb_sda = SIOD_GPIO_NUM,
        .pin_sccb_scl = SIOC_GPIO_NUM,
        .pin_d7 = Y9_GPIO_NUM,
        .pin_d6 = Y8_GPIO_NUM,
        .pin_d5 = Y7_GPIO_NUM,
        .pin_d4 = Y6_GPIO_NUM,
        .pin_d3 = Y5_GPIO_NUM,
        .pin_d2 = Y4_GPIO_NUM,
        .pin_d1 = Y3_GPIO_NUM,
        .pin_d0 = Y2_GPIO_NUM,
        .pin_vsync = VSYNC_GPIO_NUM,
        .pin_href = HREF_GPIO_NUM,
        .pin_pclk = PCLK_GPIO_NUM,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_RGB565,     
        .frame_size = FRAMESIZE_96X96,
        .jpeg_quality = 12,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s && s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
    }

    ESP_LOGI(TAG, "Camera initialized in RGB565 96x96 mode");
    return ESP_OK;
}

// ============================================================
// MEMORY MANAGEMENT
// ============================================================
static bool allocate_buffers(void) {
    size_t rgb_size = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 3;
    g_rgb_buffer = (uint8_t*)heap_caps_aligned_alloc(16, rgb_size, MALLOC_CAP_INTERNAL);
    
    if (!g_rgb_buffer) {
        ESP_LOGE(TAG, "Buffer allocation FAILED");
        return false;
    }
    
    ESP_LOGI(TAG, "Buffers allocated: %zu bytes", rgb_size);
    return true;
}

// ============================================================
// IMAGE CAPTURE AND PROCESSING
// ============================================================
static bool capture_image(void) {
    if (!xSemaphoreTake(camera_mutex, pdMS_TO_TICKS(1000))) {
        return false;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        xSemaphoreGive(camera_mutex);
        return false;
    }

    // Decompress JPEG to RGB888
    bool success = fmt2rgb888(fb->buf, fb->len, fb->format, g_rgb_buffer);
    
    esp_camera_fb_return(fb);
    xSemaphoreGive(camera_mutex);

    return success;
}

static int get_image_data(size_t offset, size_t length, float *out_ptr) {
    size_t total = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    
    if (offset + length > total) {
        return -1;
    }
    
    // fmt2rgb888 outputs BGR order
    for (size_t i = 0; i < length; i++) {
        uint8_t b = g_rgb_buffer[(offset + i) * 3 + 0];
        uint8_t g = g_rgb_buffer[(offset + i) * 3 + 1];
        uint8_t r = g_rgb_buffer[(offset + i) * 3 + 2];
        
        uint32_t pixel = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        out_ptr[i] = (float)pixel;
    }
    
    return 0;
}

// ============================================================
// AI INFERENCE
// ============================================================
static void process_detections(const ei_impulse_result_t *result) {
    ESP_LOGI(TAG, "=== DETECTIONS ===");
    
    for (size_t i = 0; i < result->bounding_boxes_count; i++) {
        const auto &bb = result->bounding_boxes[i];
        
        if (bb.value >= CONFIDENCE_THRESHOLD) {
            ESP_LOGI(TAG, "  %s: %.0f%% at [%d,%d]", 
                     bb.label, bb.value * 100.0f, bb.x, bb.y);
            
            // Count vehicles
            if (strcmp(bb.label, "car") == 0) {
                car_count++;
            } else if (strcmp(bb.label, "motorcycle") == 0) {
                motorcycle_count++;
            } else if (strcmp(bb.label, "big_vehicle") == 0) {
                big_vehicle_count++;
            }
        }
    }
    
    if (result->bounding_boxes_count == 0) {
        ESP_LOGI(TAG, "  (no detections)");
    }
    
    ESP_LOGI(TAG, "TOTALS: cars=%d, motorcycles=%d, big_vehicles=%d", 
             car_count, motorcycle_count, big_vehicle_count);
}

static void inference_task(void* arg) {
    ESP_LOGI(TAG, "Inference task started");
    
    ei_impulse_result_t result = {0};
    
    while (true) {
        int64_t start = esp_timer_get_time();

        if (!capture_image()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        ei::signal_t signal = {0}; 
        signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT; 
        signal.get_data = &get_image_data;

        if (run_classifier(&signal, &result, false) == EI_IMPULSE_OK) {
            int64_t elapsed = (esp_timer_get_time() - start) / 1000;
            ESP_LOGI(TAG, "Inference: %lld ms", elapsed);
            process_detections(&result);
        }

        vTaskDelay(pdMS_TO_TICKS(INFERENCE_DELAY_MS));
    }
}

// ============================================================
// MAIN APPLICATION
// ============================================================
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== Traffic Counter Starting ===");

    camera_mutex = xSemaphoreCreateMutex();
    result_mutex = xSemaphoreCreateMutex();
    
    wifi_init();
    if (wait_for_wifi(10)) {
         start_stream_server();  
    }

    if (camera_init() != ESP_OK) {
        ESP_LOGE(TAG, "Camera failed!");
        return;
    }

    if (!allocate_buffers()) {
        ESP_LOGE(TAG, "Memory failed!");
        return;
    }

    xTaskCreatePinnedToCore(inference_task, "inference", 16384, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "=== System Ready ===");
}