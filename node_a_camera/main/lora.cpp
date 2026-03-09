#include "lora.h"
#include "esp_log.h"

static const char *TAG = "LORA";

void lora_init(void)
{
    ESP_LOGW(TAG, "LoRa transport not implemented yet (stub)");
}

void lora_publish_counts(void)
{
    // TODO: send counts via LoRa P2P payload
}

void lora_publish_status(void)
{
    // TODO: send status via LoRa P2P payload
}
