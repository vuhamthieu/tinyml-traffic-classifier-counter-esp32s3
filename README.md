# TinyML Traffic Classifier & Counter — ESP32-S3

Real-time vehicle detection and counting on an ESP32-S3 using a custom-trained object detection model deployed entirely on-device.

## What it does

- Detects **Big Vehicles**, **Cars**, and **Motorcycles** from a live camera feed
- Counts vehicles crossing a virtual line drawn across the frame
- Streams live annotated MJPEG video over HTTP
- Publishes counts and status to MQTT

No cloud inference — everything runs on the ESP32-S3 at ~6 FPS.

## Hardware

| Component | Details |
|---|---|
| MCU | ESP32-S3 (8MB PSRAM, 16MB Flash) |
| Camera | OV3660 |
| Framework | ESP-IDF v5.x |

## Model

**ESPDet-Pico** — 224×224 INT8 quantized, trained with Ultralytics YOLOv8n and converted via esp-dl.

| Class | Score threshold |
|---|---|
| Big Vehicle | 0.75 |
| Car | 0.55 |
| Motorcycle | 0.60 |

The compiled model binary (`.espdl`) is flashed to a dedicated partition. See [firmware/model/README.md](firmware/model/README.md) for flashing instructions.

## Repository structure

```
├── firmware/       # ESP-IDF project (main application)
│   ├── main/       # Application source — modular C++ files
│   ├── components/ # vehicle_detect component (model + detector)
│   └── model/      # Model binary + flash script
└── cloud/          # Docker MQTT broker (coming soon)
```

## Quick start

### 1. Copy secrets

```bash
cp firmware/main/secrets.h.example firmware/main/secrets.h
# Edit secrets.h with your WiFi SSID, password, and MQTT broker IP
```

### 2. Flash the model

```bash
cd firmware
bash model/flash_model.sh
```

### 3. Build and flash

```bash
cd firmware
source ~/.espressif/esp-idf/export.sh
idf.py build flash monitor
```

### 4. Open the dashboard

Navigate to `http://<device-ip>/` in a browser.

## HTTP endpoints

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Live dashboard with counters + stream |
| `/stream` | GET | MJPEG video stream |
| `/counts` | GET | JSON vehicle counts + stats |
| `/reset` | POST | Reset all counters |
| `/capture` | GET | Single JPEG snapshot |
| `/preview` | GET | Fullscreen preview page |

## MQTT topics

| Topic | Payload |
|---|---|
| `traffic/counts` | `{"big_vehicle":0,"car":5,"motorcycle":3,"total":8}` |
| `traffic/status` | `{"fps":6.6,"free_heap":123456,"tracking":2}` |

## License

MIT
