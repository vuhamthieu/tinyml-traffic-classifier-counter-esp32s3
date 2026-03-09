#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "dl_detect_base.hpp"
#include "dl_image_define.hpp"
#include "espdet_detect.hpp"

#include "config.h"
#include "camera.h"
#include "motion.h"
#include "tracker.h"
#include "overlay.h"
#include "jpeg_stream.h"
#include "wifi.h"
#include "mqtt.h"
#include "telemetry.h"
#include "http_server.h"

static const char *TAG = "TRAFFIC";

#define NUM_BUFS 2

static uint8_t *g_rgb888_buf[NUM_BUFS]    = {nullptr, nullptr};
static uint8_t *g_gray_buf[NUM_BUFS]      = {nullptr, nullptr};
static uint8_t *g_raw_frame_buf[NUM_BUFS] = {nullptr, nullptr};
static bool     g_frame_valid[NUM_BUFS]   = {true, true};

static QueueHandle_t g_free_q  = nullptr;
static QueueHandle_t g_infer_q = nullptr;

static dl::detect::DetectWrapper *g_detector = nullptr;

static void on_vehicle_counted(int class_id, uint32_t new_count)
{
    (void)class_id; (void)new_count;
    telemetry_publish_counts();
}

static void grab_task(void *arg)
{
    uint8_t idx;
    while (true) {
        if (xQueueReceive(g_free_q, &idx, portMAX_DELAY) != pdTRUE) continue;

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            g_frame_valid[idx] = false;
            xQueueSend(g_free_q, &idx, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        int actual_h = (fb->width > 0) ? (int)(fb->len / ((size_t)fb->width * 2)) : 0;
        // Reject truncated frames: a partial frame compared to a full previous
        // frame produces near-100% motion -> false shake -> update_tracking blocked.
        g_frame_valid[idx] = (actual_h == CAMERA_H);
        if (!g_frame_valid[idx]) {
            ESP_LOGW(TAG, "Truncated frame %ux%d (expected %d rows), skipping",
                     (unsigned)fb->width, actual_h, CAMERA_H);
            esp_camera_fb_return(fb);
            xQueueSend(g_infer_q, &idx, portMAX_DELAY);
            continue;
        }

        rgb565_to_rgb888_gray(fb->buf, fb->width, actual_h,
                              g_rgb888_buf[idx], g_gray_buf[idx],
                              MODEL_W, MODEL_H);

        static uint32_t jpeg_cnt = 0;
        if (g_stream_active && g_raw_frame_buf[idx] && ((++jpeg_cnt & 1) == 0)) {
            memcpy(g_raw_frame_buf[idx], fb->buf, fb->len);
            draw_overlay(g_raw_frame_buf[idx], fb->width, actual_h);
            jpeg_stream_encode(g_raw_frame_buf[idx], fb->len, fb->width, actual_h);
        }

        esp_camera_fb_return(fb);
        xQueueSend(g_infer_q, &idx, portMAX_DELAY);
    }
}

static void infer_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));

    for (int i = 0; i < NUM_BUFS; i++) {
        g_rgb888_buf[i] = (uint8_t *)heap_caps_malloc(
            MODEL_W * MODEL_H * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        g_gray_buf[i] = (uint8_t *)heap_caps_malloc(
            MODEL_W * MODEL_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        g_raw_frame_buf[i] = (uint8_t *)heap_caps_malloc(
            CAMERA_W * CAMERA_H * 2, MALLOC_CAP_SPIRAM);
        if (!g_rgb888_buf[i] || !g_gray_buf[i]) {
            ESP_LOGE(TAG, "PSRAM alloc failed slot %d", i);
            vTaskDelete(NULL); return;
        }
        uint8_t seed = (uint8_t)i;
        xQueueSend(g_free_q, &seed, portMAX_DELAY);
    }

    ESP_LOGI(TAG, "Creating detector");
    g_detector = new ESPDetDetect();
    ESP_LOGI(TAG, "PSRAM free: %lu", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    xTaskCreatePinnedToCore(grab_task, "grab", 8192, NULL, 4, NULL, 0);

    uint32_t frame_count = 0;
    int64_t  last_stats  = esp_timer_get_time();
    uint8_t  buf_idx;
    int det_frames = 0, det_total = 0;

    while (true) {
        if (xQueueReceive(g_infer_q, &buf_idx, portMAX_DELAY) != pdTRUE) continue;

        if (!g_frame_valid[buf_idx]) {
            xQueueSend(g_free_q, &buf_idx, portMAX_DELAY);
            continue;
        }

        uint8_t motion_mask[MOTION_GRID_H][MOTION_GRID_W];
        if (g_gray_buf[buf_idx])
            compute_motion_mask(g_gray_buf[buf_idx], motion_mask);
        else
            memset(motion_mask, 0, sizeof(motion_mask));

        motion_update_prev(g_gray_buf[buf_idx]);

        int active_cells = 0;
        for (int gy = 0; gy < MOTION_GRID_H; gy++)
            for (int gx = 0; gx < MOTION_GRID_W; gx++)
                active_cells += motion_mask[gy][gx];
        const bool shake = (active_cells * 10 > MOTION_GRID_H * MOTION_GRID_W * 9);

        dl::image::img_t img = {
            .data     = (void *)g_rgb888_buf[buf_idx],
            .width    = MODEL_W,
            .height   = MODEL_H,
            .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888,
        };

        int64_t t0 = esp_timer_get_time();
        auto &results = g_detector->run(img);
        int   inf_ms  = (int)((esp_timer_get_time() - t0) / 1000);

        static uint32_t dbg_cnt = 0;
        if ((++dbg_cnt % 30) == 0)
            jpeg_encode_debug_frame(g_rgb888_buf[buf_idx], MODEL_W, MODEL_H);

        xQueueSend(g_free_q, &buf_idx, portMAX_DELAY);

        det_frames++; det_total += (int)results.size();

        if (!shake && !results.empty())
            update_tracking(results, motion_mask);

        overlay_update_boxes(tracked_objects, num_tracked_objects);

        frame_count++;
        int64_t now = esp_timer_get_time();
        if (now - last_stats >= 10000000LL) {
            current_fps = (float)frame_count * 1e6f / (float)(now - last_stats);
            xSemaphoreTake(counter_mutex, portMAX_DELAY);
            ESP_LOGW(TAG, "FPS=%.1f | Car=%lu Moto=%lu | tracks=%d | inf=%dms | dets=%d/10f motion=%d",
                     current_fps,
                     (unsigned long)vehicle_counts[CLASS_CAR],
                     (unsigned long)vehicle_counts[CLASS_MOTORCYCLE],
                     num_tracked_objects, inf_ms,
                     (det_frames > 0 ? det_total * 10 / det_frames : 0),
                     active_cells);
            xSemaphoreGive(counter_mutex);
            telemetry_publish_status();
            det_frames = 0; det_total = 0;
            frame_count = 0;
            last_stats  = now;
        }
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Smart Traffic Counter (ESP-DL ESPDet-Pico)");
    ESP_LOGI(TAG, "Heap: %lu  PSRAM: %lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    counter_mutex = xSemaphoreCreateMutex();
    jpeg_stream_init();
    g_free_q  = xQueueCreate(NUM_BUFS, sizeof(uint8_t));
    g_infer_q = xQueueCreate(NUM_BUFS, sizeof(uint8_t));

    if (!camera_init()) return;

    tracker_set_count_callback(on_vehicle_counted);

    wifi_init();
    vTaskDelay(pdMS_TO_TICKS(3000));
    telemetry_init();
    start_webserver();

    esp_task_wdt_config_t wdt = {.timeout_ms = 20000, .idle_core_mask = 0, .trigger_panic = false};
    esp_task_wdt_reconfigure(&wdt);

    xTaskCreatePinnedToCore(infer_task, "infer", 32768, NULL, 5, NULL, 1);
    ESP_LOGW(TAG, "Ready — http://<device-ip>/");
}
