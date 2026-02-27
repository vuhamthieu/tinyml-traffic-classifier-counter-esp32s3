#include "mqtt.h"
#include "config.h"
#include "tracker.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t g_mqtt          = nullptr;
static volatile bool            g_mqtt_connected = false;

void mqtt_publish_counts(void)
{
    if (!g_mqtt || !g_mqtt_connected) return;
    char buf[128];
    xSemaphoreTake(counter_mutex, portMAX_DELAY);
    int len = snprintf(buf, sizeof(buf),
        "{\"big_vehicle\":%lu,\"car\":%lu,\"motorcycle\":%lu,\"total\":%lu}",
        (unsigned long)vehicle_counts[CLASS_BIG_VEHICLE],
        (unsigned long)vehicle_counts[CLASS_CAR],
        (unsigned long)vehicle_counts[CLASS_MOTORCYCLE],
        (unsigned long)(vehicle_counts[0] + vehicle_counts[1] + vehicle_counts[2]));
    xSemaphoreGive(counter_mutex);
    esp_mqtt_client_publish(g_mqtt, MQTT_TOPIC_COUNTS, buf, len, 0, 0);
}

void mqtt_publish_status(void)
{
    if (!g_mqtt || !g_mqtt_connected) return;
    char buf[160];
    int len = snprintf(buf, sizeof(buf),
        "{\"fps\":%.1f,\"free_heap\":%lu,\"tracking\":%d}",
        current_fps, (unsigned long)esp_get_free_heap_size(), current_tracking);
    esp_mqtt_client_publish(g_mqtt, MQTT_TOPIC_STATUS, buf, len, 0, 0);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch ((esp_mqtt_event_id_t)id) {
        case MQTT_EVENT_CONNECTED:
            g_mqtt_connected = true;
            ESP_LOGW(TAG, "MQTT connected");
            mqtt_publish_counts();
            break;
        case MQTT_EVENT_DISCONNECTED:
            g_mqtt_connected = false;
            break;
        case MQTT_EVENT_ERROR:
            g_mqtt_connected = false;
            ESP_LOGE(TAG, "MQTT error");
            break;
        default: break;
    }
}

void mqtt_init(void)
{
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri    = MQTT_BROKER_URI;
    cfg.credentials.client_id = MQTT_CLIENT_ID;
    cfg.network.timeout_ms    = 3000;
    g_mqtt = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(g_mqtt, MQTT_EVENT_ANY, mqtt_event_handler, nullptr);
    esp_mqtt_client_start(g_mqtt);
}
