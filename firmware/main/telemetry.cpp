#include "telemetry.h"
#include "config.h"
#include "mqtt.h"
#include "lora.h"

void telemetry_init(void)
{
#if TELEMETRY_USE_MQTT
    mqtt_init();
#endif
#if TELEMETRY_USE_LORA
    lora_init();
#endif
}

void telemetry_publish_counts(void)
{
#if TELEMETRY_USE_MQTT
    mqtt_publish_counts();
#endif
#if TELEMETRY_USE_LORA
    lora_publish_counts();
#endif
}

void telemetry_publish_status(void)
{
#if TELEMETRY_USE_MQTT
    mqtt_publish_status();
#endif
#if TELEMETRY_USE_LORA
    lora_publish_status();
#endif
}
