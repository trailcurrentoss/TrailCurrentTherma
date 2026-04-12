#!/bin/bash
# Flash the Therma Controller over USB.
#
#   PORT=/dev/ttyACM0 ./scripts/flash_controller.sh
#
# Defaults to /dev/ttyACM0 if PORT is unset.
set -e
cd "$(dirname "$0")/.."
PORT="${PORT:-/dev/ttyACM0}"
idf.py -C controller -B controller/build -p "$PORT" flash monitor
