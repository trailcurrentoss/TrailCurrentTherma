#!/bin/bash
# Build all three TrailCurrentTherma firmwares.
#
# Produces two binaries for each firmware:
#   *.bin         — app-only (for OTA, where supported)
#   *_merged.bin  — full flash image (for the web flasher, flashable at 0x0)
#
# The controller has OTA partitions; the relay boards do not (they are
# flashed via USB only — either the browser web flasher or esptool.py).
#
# Usage:
#   ./build-all.sh
#
# Attach all six produced binaries to the GitHub release so the web
# flasher and OTA flow can both find them.

set -e

CHIP="esp32s3"
FLASH_MODE="dio"
FLASH_SIZE="4MB"

# Override any inherited IDF_TARGET from the user's shell. If this is left set
# to a different chip (e.g. esp32), idf.py refuses to build with the message
# "Project sdkconfig ... was generated for target 'esp32s3', but environment
# variable IDF_TARGET is set to 'esp32'".
export IDF_TARGET="$CHIP"

cd "$(dirname "$0")"

# `idf.py build` will pick the default target (often esp32) on first run if
# nothing has set it yet, and once a sdkconfig exists for the wrong target it
# sticks. Force the target before each build. ensure_target() is a no-op when
# the existing sdkconfig already has CONFIG_IDF_TARGET="$CHIP".
ensure_target() {
    local proj="$1"
    local cfg="${proj}/sdkconfig"
    if [ -f "$cfg" ] && grep -q "^CONFIG_IDF_TARGET=\"${CHIP}\"" "$cfg"; then
        return 0
    fi
    echo "Setting target to ${CHIP} for ${proj}/ ..."
    rm -f "$cfg"
    rm -rf "${proj}"/build*
    idf.py -C "$proj" set-target "$CHIP"
}

ensure_target controller
ensure_target relay

# ---------------------------------------------------------------------------
# 1. Controller (Waveshare ESP32-S3-RS485-CAN, OTA-capable)
# ---------------------------------------------------------------------------
echo "========================================"
echo "Building Therma Controller ..."
echo "========================================"
idf.py -C controller -B controller/build build

b="controller/build"
cp "$b/trailcurrent_therma_controller.bin" "$b/therma_controller.bin"
esptool.py --chip "$CHIP" merge_bin \
    -o "$b/therma_controller_merged.bin" \
    --flash_mode "$FLASH_MODE" --flash_size "$FLASH_SIZE" \
    0x0     "$b/bootloader/bootloader.bin" \
    0x8000  "$b/partition_table/partition-table.bin" \
    0xe000  "$b/ota_data_initial.bin" \
    0x10000 "$b/therma_controller.bin"
echo ""

# ---------------------------------------------------------------------------
# 2. Heater relay and 3. Cooler relay (Waveshare ESP32-S3-Relay-1CH, no OTA)
# ---------------------------------------------------------------------------
build_relay_role() {
    local role="$1"
    local lower
    lower="$(echo "$role" | tr '[:upper:]' '[:lower:]')"
    local out="relay/build_${lower}"

    echo "========================================"
    echo "Building Therma ${role} Relay ..."
    echo "========================================"
    idf.py -C relay -B "$out" -DTHERMA_RELAY_ROLE="$role" build

    cp "$out/trailcurrent_therma_relay.bin" "$out/therma_${lower}_relay.bin"
    esptool.py --chip "$CHIP" merge_bin \
        -o "$out/therma_${lower}_relay_merged.bin" \
        --flash_mode "$FLASH_MODE" --flash_size "$FLASH_SIZE" \
        0x0     "$out/bootloader/bootloader.bin" \
        0x8000  "$out/partition_table/partition-table.bin" \
        0x10000 "$out/therma_${lower}_relay.bin"
    echo ""
}

build_relay_role HEATER
build_relay_role COOLER

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo "========================================"
echo "Build complete"
echo "========================================"
echo ""
echo "App-only binaries (for OTA):"
ls -lh controller/build/therma_controller.bin \
       relay/build_heater/therma_heater_relay.bin \
       relay/build_cooler/therma_cooler_relay.bin
echo ""
echo "Merged binaries (for web flasher):"
ls -lh controller/build/therma_controller_merged.bin \
       relay/build_heater/therma_heater_relay_merged.bin \
       relay/build_cooler/therma_cooler_relay_merged.bin
echo ""
echo "Attach ALL six binaries to the GitHub release."
