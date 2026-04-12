#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Persists the user-adjustable thermostat setpoint and hysteresis
// threshold in NVS so a power cycle does not lose them. Defaults
// (board.h) are loaded if no stored values exist.

esp_err_t state_store_init(void);

int16_t state_store_get_setpoint_deci_c(void);
uint8_t state_store_get_threshold_deci_c(void);

// Returns true if the value was accepted (within safe range) and the
// stored state was updated. Writes to NVS. No-op if the value is
// identical to the current stored value.
bool state_store_set_setpoint_deci_c(int16_t setpoint_deci_c);
bool state_store_set_threshold_deci_c(uint8_t threshold_deci_c);
