#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "relay_io.h"

// Fault flags carried in the ThermaStatus frame (byte 5).
#define THERMA_FAULT_SENSOR_STALE       (1 << 0)
#define THERMA_FAULT_HEAT_FB_MISMATCH   (1 << 1)
#define THERMA_FAULT_COOL_FB_MISMATCH   (1 << 2)

typedef struct {
    therma_mode_t mode;
    int16_t       current_deci_c;   // most recent Borealis reading
    bool          heat_fb;
    bool          cool_fb;
    uint8_t       fault;
    bool          current_valid;
} therma_status_t;

void thermostat_init(void);

// Called whenever an EnvironmentSensorData (0x1F) frame arrives.
void thermostat_on_current_temperature(int16_t current_deci_c);

// Run one iteration of the control loop. Called periodically (~1 Hz)
// from the CAN task. Reads setpoint/threshold from state_store, reads
// sensor timestamps from its own state, computes the new mode, and
// drives relay_io. Populates *out with the latest status snapshot so
// the CAN task can encode it into a 0x40 frame.
void thermostat_tick(therma_status_t *out);
