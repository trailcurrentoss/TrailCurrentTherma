#pragma once

#include "driver/gpio.h"
#include "driver/twai.h"

// --- Waveshare ESP32-S3-RS485-CAN pin assignments ---

#define CAN_TX_PIN              GPIO_NUM_15
#define CAN_RX_PIN              GPIO_NUM_16

// Inter-board signalling to the heater and cooler relay boards.
// Each relay board gets one command out (controller drives it) and
// one status in (relay board drives it). 3V3 logic on both ends.
#define HEAT_CMD_OUT_PIN        GPIO_NUM_4
#define HEAT_STATUS_IN_PIN      GPIO_NUM_5
#define COOL_CMD_OUT_PIN        GPIO_NUM_6
#define COOL_STATUS_IN_PIN      GPIO_NUM_7

// --- CAN protocol IDs ---

// Existing cross-project IDs (same as Bearing/Switchback).
#define CAN_ID_OTA                        0x00
#define CAN_ID_WIFI_CONFIG                0x01
#define CAN_ID_DISCOVERY_TRIGGER          0x02
#define CAN_ID_VERSION                    0x04

// Current ambient temperature, broadcast by Borealis (consume only).
// Byte 0 = temperature in whole degrees C, signed int8.
// (See TrailCurrentDocumentation/10_Reference/CAN_BUS_REFERENCE.md)
#define CAN_ID_BOREALIS_ENVIRONMENT       0x1F

// Therma controller authoritative broadcasts.
// 0x3F ThermaDesiredTemperature — Therma is source of truth.
//   bytes 0-1: setpoint in 0.1 C, int16 little-endian
//   byte  2:   threshold (deadband) in 0.1 C, uint8
#define CAN_ID_THERMA_DESIRED             0x3F

// 0x40 ThermaStatus — Therma is source of truth for mode.
//   byte 0: mode (0=idle, 1=heating, 2=cooling)
//   bytes 1-2: current temp in 0.1 C, int16 little-endian
//   byte 3: heat relay feedback (0/1)
//   byte 4: cool relay feedback (0/1)
//   byte 5: fault bitfield (b0=sensor_stale, b1=heat_fb_mismatch, b2=cool_fb_mismatch)
#define CAN_ID_THERMA_STATUS              0x40

// 0x41 SetDesiredTemperatureRequest — any device on the bus.
//   bytes 0-1: desired setpoint in 0.1 C, int16 little-endian
// Therma validates, clamps to safe range, persists to NVS, then
// re-broadcasts authoritative truth on 0x3F next tick.
#define CAN_ID_THERMA_SET_DESIRED         0x41

// 0x42 SetThresholdRequest — any device on the bus.
//   byte 0: threshold in 0.1 C, uint8 (0-25.5 C)
#define CAN_ID_THERMA_SET_THRESHOLD       0x42

// --- CAN baud rate ---
#define CAN_BAUD_RATE                     500  // kbps

// --- Thermostat tuning ---

// Temperature is stored and transmitted in 0.1 C units throughout.
#define TEMP_SCALE                        10

// Defaults (overridable via NVS + CAN change-request)
#define THERMA_DEFAULT_SETPOINT_DECI_C    210    // 21.0 C
#define THERMA_DEFAULT_THRESHOLD_DECI_C   5      // 0.5 C deadband

// Safety clamps on accepted setpoint/threshold values
#define THERMA_MIN_SETPOINT_DECI_C        50     // 5.0 C
#define THERMA_MAX_SETPOINT_DECI_C        350    // 35.0 C
#define THERMA_MIN_THRESHOLD_DECI_C       1      // 0.1 C
#define THERMA_MAX_THRESHOLD_DECI_C       50     // 5.0 C

// If no EnvironmentSensorData frames arrive for this long, treat the
// sensor as stale and drop both outputs to idle (fail-safe off).
#define SENSOR_STALE_TIMEOUT_MS           10000

// Minimum time to remain in a non-idle state (chatter guard).
#define THERMA_MIN_DWELL_MS               5000

// Minimum gap between raising one cmd pin and the other, to guarantee
// non-overlap of the heater and cooler drives under scheduling jitter.
#define THERMA_SWAP_DEAD_TIME_MS          50
