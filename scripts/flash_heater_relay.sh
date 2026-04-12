#!/bin/bash
# Flash the Therma Heater Relay over USB.
#
#   PORT=/dev/ttyACM1 ./scripts/flash_heater_relay.sh
#
# Defaults to /dev/ttyACM1.
set -e
cd "$(dirname "$0")/.."
PORT="${PORT:-/dev/ttyACM1}"
idf.py -C relay -B relay/build_heater -DTHERMA_RELAY_ROLE=HEATER -p "$PORT" flash monitor
