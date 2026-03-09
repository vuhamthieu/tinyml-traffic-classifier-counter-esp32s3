#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# flash_model.sh  —  Write vehicle_detect.espdl to the 'vehicle_det' partition
#
# Usage:
#   ./model/flash_model.sh                          # defaults: auto-detect port, default model
#   ./model/flash_model.sh /dev/ttyUSB0             # explicit port
#   ./model/flash_model.sh /dev/ttyUSB0 my_model.espdl
#
# Prerequisites:
#   source ~/.espressif/esp-idf/export.sh
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PORT="${1:-}"
MODEL_FILE="${2:-${SCRIPT_DIR}/vehicle_detect.espdl}"

# ── Validate model file ───────────────────────────────────────────────────────
if [[ ! -f "${MODEL_FILE}" ]]; then
    echo ""
    echo "  ERROR: Model file not found: ${MODEL_FILE}"
    echo ""
    echo "  Train your model first:"
    echo "    python espdet_run.py --class_name car motorcycle \\"
    echo "      --size 224 224 --target esp32s3 --output vehicle_detect"
    echo "  Then copy vehicle_detect.espdl → firmware/model/"
    echo ""
    exit 1
fi

MODEL_SIZE=$(stat -c%s "${MODEL_FILE}" 2>/dev/null || stat -f%z "${MODEL_FILE}")
echo "  Model:  ${MODEL_FILE}  ($(( MODEL_SIZE / 1024 )) KB)"

# ── Auto-detect port if not provided ─────────────────────────────────────────
if [[ -z "${PORT}" ]]; then
    for candidate in /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyACM0 /dev/cu.usbserial*; do
        if [[ -e "${candidate}" ]]; then PORT="${candidate}"; break; fi
    done
    if [[ -z "${PORT}" ]]; then
        echo "  ERROR: Could not auto-detect serial port. Pass it as first argument."
        exit 1
    fi
fi
echo "  Port:   ${PORT}"

# ── Resolve partition offset from partitions.csv ─────────────────────────────
PART_CSV="${FIRMWARE_DIR}/partitions.csv"
OFFSET=$(grep '^vehicle_det' "${PART_CSV}" | awk -F',' '{gsub(/[ \t]/, "", $4); print $4}')
if [[ -z "${OFFSET}" ]]; then
    echo "  ERROR: 'vehicle_det' partition not found in ${PART_CSV}"
    exit 1
fi
echo "  Partition 'vehicle_det' offset: ${OFFSET}"
echo ""

# ── Flash ─────────────────────────────────────────────────────────────────────
parttool.py --port "${PORT}" \
    write_partition \
    --partition-name vehicle_det \
    --input "${MODEL_FILE}"

echo ""
echo "  ✓  Model flashed successfully to partition 'vehicle_det'"
echo "     Reset the device to reload the model."
