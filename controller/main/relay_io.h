#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    THERMA_MODE_IDLE    = 0,
    THERMA_MODE_HEATING = 1,
    THERMA_MODE_COOLING = 2,
} therma_mode_t;

esp_err_t relay_io_init(void);

// Drive the command pins to match the requested mode. Enforces the
// mutual-exclusion invariant by always dropping the other pin first
// and waiting THERMA_SWAP_DEAD_TIME_MS before raising the new one.
// Safe to call from any task.
void relay_io_set_mode(therma_mode_t mode);

therma_mode_t relay_io_get_commanded_mode(void);

// Reads the feedback pins driven by the two relay boards.
bool relay_io_read_heat_feedback(void);
bool relay_io_read_cool_feedback(void);
