# Model Training & Integration Guide

## What you get from Colab training

There is **no `model_data.h`** — that is Edge Impulse / TFLite style.  
ESP-DL uses a binary `.espdl` format together with generated C++ wrapper files.

After running `espdet_run.py`, you get a folder called **`models/vehicle_detect/`** which is a complete IDF component:

```
models/vehicle_detect/           ← copy this whole folder to firmware/components/vehicle_detect/
  espdet_detect.hpp              ← C++ class declaration  (ESPDetDetect)
  espdet_detect.cpp              ← C++ implementation
  CMakeLists.txt                 ← auto-packs + auto-flashes the model
  Kconfig                        ← menuconfig options
  idf_component.yml
  models/
    s3/
      espdet_pico_224_224_vehicle.espdl   ← TRAINED MODEL BINARY (goes here)
    p4/
      (empty — not needed for ESP32-S3)
```

---

## Step-by-step: Train on Google Colab

### 1. Clone esp-detection on Colab

```bash
!git clone https://github.com/espressif/esp-detection
%cd esp-detection
!pip install -r requirements.txt
```

### 2. Prepare your dataset (YOLO format)

Directory structure expected:
```
dataset/
  images/
    train/  *.jpg
    val/    *.jpg
  labels/
    train/  *.txt   (YOLO format: class cx cy w h, normalised 0–1)
    val/    *.txt
```

**Class IDs must be:**
| ID | Label       |
|----|-------------|
| 0  | car         |
| 1  | motorcycle  |

Create `cfg/datasets/vehicle.yaml`:
```yaml
path: /content/esp-detection/dataset
train: images/train
val:   images/val
nc: 2
names: [car, motorcycle]
```

### 3. Run training

```bash
!python espdet_run.py \
  --class_name vehicle \
  --dataset cfg/datasets/vehicle.yaml \
  --size 224 224 \
  --target esp32s3 \
  --calib_data deploy/vehicle_calib \
  --espdl espdet_pico_224_224_vehicle.espdl \
  --img sample.jpg
```

> ⚠️ Use exactly `--class_name vehicle` — this controls the generated Kconfig symbols and enum names that this project expects.

### 4. Download the generated component

Download the entire `models/vehicle_detect/` folder from Colab (zip it first):
```bash
!zip -r vehicle_detect_component.zip models/vehicle_detect/
```

---

## Integration (one-time, after training)

```
firmware/
  components/
    vehicle_detect/          ← REPLACE all files with your downloaded folder
      espdet_detect.hpp
      espdet_detect.cpp
      CMakeLists.txt
      Kconfig
      idf_component.yml
      models/
        s3/
          espdet_pico_224_224_vehicle.espdl   ← MODEL FILE GOES HERE
```

Then build and flash — the CMakeLists handles everything automatically:

```bash
cd firmware
source ~/.espressif/esp-idf/export.sh
idf.py build flash monitor
```

`idf.py flash` will:
1. Pack the `.espdl` binary
2. Flash the app firmware
3. Flash the model to the **`espdet_det`** partition (1 MB at offset 0x310000)

No `parttool.py`, no manual steps.

---

## Expected Performance (ESP32-S3, 224×224)

| Phase          | Latency  |
|----------------|----------|
| Pre-processing | ~51 ms   |
| Inference      | ~123 ms  |
| Post-processing| ~3 ms    |
| **Total**      | **~177 ms → ~5–6 FPS** |

Source: [espressif/esp-detection README](https://github.com/espressif/esp-detection)

