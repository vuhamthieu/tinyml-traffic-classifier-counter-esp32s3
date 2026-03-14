#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LoRa.h>
#include <SPI.h>
#include <time.h>
#include "secrets.h"

namespace {

constexpr long LORA_FREQUENCY_HZ            = 433E6;
constexpr long LORA_SIGNAL_BANDWIDTH_HZ     = 125E3;
constexpr int  LORA_SPREADING_FACTOR        = 7;
constexpr int  LORA_CODING_RATE_DENOMINATOR = 5;
constexpr int  LORA_SYNC_WORD               = 0x12;
constexpr int  LORA_TX_POWER_DBM            = 17;

constexpr int LORA_PIN_NSS  = 5;   // D1
constexpr int LORA_PIN_RST  = 16;  // D0
constexpr int LORA_PIN_DIO0 = 4;   // D2
constexpr size_t MAX_LORA_PAYLOAD_BYTES        = 220;

constexpr unsigned long WIFI_RETRY_INTERVAL_MS     = 5000;
constexpr unsigned long MQTT_RETRY_INTERVAL_MS     = 5000;
constexpr unsigned long GATEWAY_HEALTH_INTERVAL_MS = 30000;
constexpr unsigned long IDLE_LOG_INTERVAL_MS       = 10000;
constexpr unsigned long LED_BLINK_INTERVAL_MS      = 1000;

BearSSL::WiFiClientSecure espClient;
PubSubClient mqtt(espClient);

unsigned long lastWifiAttemptMs   = 0;
unsigned long lastMqttAttemptMs   = 0;
unsigned long lastHealthPublishMs = 0;
unsigned long lastIdleLogMs       = 0;
unsigned long lastLedToggleMs     = 0;

unsigned long rxCount       = 0;
unsigned long mqttPubCount  = 0;
unsigned long mqttFailCount = 0;
bool ledState = false;

void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastWifiAttemptMs < WIFI_RETRY_INTERVAL_MS) return;
  lastWifiAttemptMs = now;
  Serial.printf("[WIFI] Connecting to %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool wifiReady() { return WiFi.status() == WL_CONNECTED; }

void mqttConnect() {
  if (!wifiReady() || mqtt.connected()) return;
  unsigned long now = millis();
  if (now - lastMqttAttemptMs < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttAttemptMs = now;

  Serial.printf("[MQTT] Connecting to %s:%d ...\n", MQTT_BROKER, MQTT_PORT);
  char clientId[24];
  snprintf(clientId, sizeof(clientId), "node_b_gw_%06X", ESP.getChipId());

  if (mqtt.connect(clientId, MQTT_USER, MQTT_PASSWORD)) {
    Serial.println(F("[MQTT] Connected!"));
    JsonDocument doc;
    doc["node"]   = "node_b";
    doc["type"]   = "online";
    doc["ip"]     = WiFi.localIP().toString();
    doc["heap"]   = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    mqtt.publish(MQTT_TOPIC_GATEWAY, buf, true);
  } else {
    Serial.printf("[MQTT] Failed rc=%d. Retry in %lus\n",
                  mqtt.state(), MQTT_RETRY_INTERVAL_MS / 1000);
  }
}

void publishLoRaPacket(const char *payload, size_t payloadLen, long rssi, float snr) {
  if (!mqtt.connected()) { mqttFailCount++; return; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, payloadLen);
  if (err) {
    Serial.printf("[MQTT] JSON parse error: %s\n", err.c_str());
    if (mqtt.publish(MQTT_TOPIC_RAW, payload, payloadLen)) mqttPubCount++;
    else mqttFailCount++;
    return;
  }

  doc["rssi"]       = rssi;
  doc["snr"]        = serialized(String(snr, 1));
  doc["gw_rx_seq"]  = rxCount;
  doc["gw_time_ms"] = millis();

  char buf[512];
  size_t len = serializeJson(doc, buf, sizeof(buf));

  const char *type  = doc["type"] | "unknown";
  const char *topic = MQTT_TOPIC_RAW;
  if (strcmp(type, "counts") == 0 || strcmp(type, "data") == 0) topic = MQTT_TOPIC_COUNTS;
  else if (strcmp(type, "status") == 0) topic = MQTT_TOPIC_STATUS;
  else if (strcmp(type, "boot") == 0)   topic = MQTT_TOPIC_STATUS;

  if (mqtt.publish(topic, buf, len)) {
    mqttPubCount++;
    Serial.printf("[MQTT] -> %s (%u B)\n", topic, len);
  } else {
    mqttFailCount++;
    Serial.println(F("[MQTT] Publish FAILED"));
  }
}

void publishGatewayHealth() {
  if (!mqtt.connected()) return;
  unsigned long now = millis();
  if (now - lastHealthPublishMs < GATEWAY_HEALTH_INTERVAL_MS) return;
  lastHealthPublishMs = now;

  JsonDocument doc;
  doc["node"]          = "node_b";
  doc["type"]          = "health";
  doc["uptime_s"]      = now / 1000;
  doc["heap"]          = ESP.getFreeHeap();
  doc["wifi_rssi"]     = WiFi.RSSI();
  doc["lora_rx_total"] = rxCount;
  doc["mqtt_pub"]      = mqttPubCount;
  doc["mqtt_fail"]     = mqttFailCount;
  doc["ip"]            = WiFi.localIP().toString();

  char buf[320];
  serializeJson(doc, buf, sizeof(buf));
  if (!mqtt.publish(MQTT_TOPIC_GATEWAY, buf)) {
    mqttFailCount++;
  }
  Serial.printf("[HEALTH] heap=%u wifi=%ddBm rx=%lu pub=%lu fail=%lu\n",
                ESP.getFreeHeap(), WiFi.RSSI(), rxCount, mqttPubCount, mqttFailCount);
}

void ledHeartbeat() {
  unsigned long now = millis();
  if (now - lastLedToggleMs < LED_BLINK_INTERVAL_MS) return;
  lastLedToggleMs = now;
  ledState = !ledState;
  digitalWrite(LED_BUILTIN, ledState ? LOW : HIGH);
}

}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(115200);
  while (!Serial) { delay(10); }
  delay(500);

  Serial.println();
  Serial.println(F("=============================================="));
  Serial.println(F("  Node B - LoRa-to-MQTT Gateway"));
  Serial.println(F("=============================================="));
  Serial.printf("Chip: %08X  Heap: %u  Flash: %uKB\n",
                ESP.getChipId(), ESP.getFreeHeap(), ESP.getFlashChipSize() / 1024);
  Serial.flush();

  Serial.println(F("[LORA] Init..."));
  SPI.begin();
  LoRa.setPins(LORA_PIN_NSS, LORA_PIN_RST, LORA_PIN_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY_HZ)) {
    Serial.println(F("[LORA] FAILED!"));
    while (true) {
      delay(3000);
      if (LoRa.begin(LORA_FREQUENCY_HZ)) break;
    }
  }
  LoRa.setSignalBandwidth(LORA_SIGNAL_BANDWIDTH_HZ);
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setCodingRate4(LORA_CODING_RATE_DENOMINATOR);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setTxPower(LORA_TX_POWER_DBM);
  LoRa.enableCrc();
  Serial.println(F("[LORA] OK - 433MHz SF7 BW125k"));

  Serial.printf("[WIFI] Connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    delay(250); Serial.print('.');
  }
  Serial.println();
  if (wifiReady()) {
    Serial.printf("[WIFI] OK  IP: %s  RSSI: %ddBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println(F("[WIFI] Not yet — will retry"));
  }

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print(F("[NTP] Syncing"));
  time_t now = time(nullptr);
  unsigned long ntpStart = millis();
  while (now < 100000 && millis() - ntpStart < 10000) {
    delay(250); Serial.print('.');
    now = time(nullptr);
  }
  Serial.println();
  if (now > 100000) {
    struct tm ti;
    localtime_r(&now, &ti);
    Serial.printf("[NTP] OK  %04d-%02d-%02d %02d:%02d:%02d\n",
                  ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday,
                  ti.tm_hour, ti.tm_min, ti.tm_sec);
  } else {
    Serial.println(F("[NTP] Timeout — TLS may fail"));
  }

  espClient.setInsecure();

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);
  mqttConnect();

  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println(F("[READY] Listening for LoRa..."));
}

void loop() {
  wifiConnect();
  if (wifiReady()) { mqttConnect(); mqtt.loop(); }
  ledHeartbeat();
  publishGatewayHealth();

  int packetSize = LoRa.parsePacket();
  if (packetSize > 0) {
    if (packetSize > static_cast<int>(MAX_LORA_PAYLOAD_BYTES)) {
      while (LoRa.available()) LoRa.read();
      mqttFailCount++;
      Serial.printf("[LORA RX] Dropped oversize packet: %d B\n", packetSize);
      delay(2);
      return;
    }

    char payload[MAX_LORA_PAYLOAD_BYTES + 1];
    size_t readLen = LoRa.readBytes(payload, packetSize);
    while (LoRa.available()) LoRa.read();
    payload[readLen] = '\0';

    long rssi  = LoRa.packetRssi();
    float snr  = LoRa.packetSnr();
    rxCount++;

    Serial.printf("[LORA RX] #%lu %dB rssi=%ld snr=%.1f\n",
                  rxCount, packetSize, rssi, snr);
    Serial.printf("  %s\n", payload);
    publishLoRaPacket(payload, readLen, rssi, snr);
  } else {
    unsigned long now = millis();
    if (now - lastIdleLogMs >= IDLE_LOG_INTERVAL_MS) {
      Serial.printf("[IDLE] rx=%lu pub=%lu fail=%lu wifi=%s mqtt=%s\n",
                    rxCount, mqttPubCount, mqttFailCount,
                    wifiReady() ? "OK" : "DOWN",
                    mqtt.connected() ? "OK" : "DOWN");
      lastIdleLogMs = now;
    }
    delay(5);
  }
}