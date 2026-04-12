#!/bin/bash
# Flash the Therma Cooler Relay over USB.
#
#   PORT=/dev/ttyACM2 ./scripts/flash_cooler_relay.sh
#
# Defaults to /dev/ttyACM2.
set -e
cd "$(dirname "$0")/.."
PORT="${PORT:-/dev/ttyACM2}"
idf.py -C relay -B relay/build_cooler -DTHERMA_RELAY_ROLE=COOLER -p "$PORT" flash monitor
