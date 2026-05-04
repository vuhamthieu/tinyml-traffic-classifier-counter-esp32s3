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

void mqtt_publish_all(uint32_t delta_car, uint32_t delta_moto)
{
    if (!g_mqtt || !g_mqtt_connected) return;
    char buf[256];
    
    int len = snprintf(buf, sizeof(buf),
        "{\"type\":\"data\",\"car\":%lu,\"motorcycle\":%lu,\"fps\":%.1f,\"heap\":%lu}",
        (unsigned long)delta_car,
        (unsigned long)delta_moto,
        current_fps, 
        (unsigned long)esp_get_free_heap_size());
    esp_mqtt_client_publish(g_mqtt, MQTT_TOPIC_COUNTS, buf, len, 0, 0);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch ((esp_mqtt_event_id_t)id) {
        case MQTT_EVENT_CONNECTED:
            g_mqtt_connected = true;
            ESP_LOGW(TAG, "MQTT connected");
            // Do not publish immediately upon connect since we want true deltas
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
