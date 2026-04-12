#include "thermostat.h"
#include "board.h"
#include "relay_io.h"
#include "state_store.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "thermostat";

static int16_t  s_current_deci_c   = 0;
static bool     s_current_valid    = false;
static int64_t  s_last_sample_us   = 0;
static int64_t  s_last_mode_change_us = 0;
static therma_mode_t s_mode = THERMA_MODE_IDLE;

void thermostat_init(void)
{
    s_current_deci_c = 0;
    s_current_valid = false;
    s_last_sample_us = 0;
    s_last_mode_change_us = esp_timer_get_time();
    s_mode = THERMA_MODE_IDLE;
    relay_io_set_mode(THERMA_MODE_IDLE);
}

void thermostat_on_current_temperature(int16_t current_deci_c)
{
    s_current_deci_c = current_deci_c;
    s_current_valid = true;
    s_last_sample_us = esp_timer_get_time();
}

static void set_mode_locked(therma_mode_t new_mode)
{
    if (new_mode == s_mode) return;
    s_mode = new_mode;
    s_last_mode_change_us = esp_timer_get_time();
    relay_io_set_mode(new_mode);
}

void thermostat_tick(therma_status_t *out)
{
    int64_t now_us = esp_timer_get_time();
    uint8_t fault = 0;

    // Sensor staleness check (fail-safe off).
    bool stale = false;
    if (!s_current_valid) {
        stale = true;
    } else if ((now_us - s_last_sample_us) > (int64_t)SENSOR_STALE_TIMEOUT_MS * 1000LL) {
        stale = true;
        fault |= THERMA_FAULT_SENSOR_STALE;
        ESP_LOGW(TAG, "Sensor stale, forcing idle");
    }

    int16_t setpoint = state_store_get_setpoint_deci_c();
    uint8_t threshold = state_store_get_threshold_deci_c();

    therma_mode_t desired = s_mode;

    if (stale) {
        desired = THERMA_MODE_IDLE;
    } else {
        int16_t below = setpoint - threshold;
        int16_t above = setpoint + threshold;

        switch (s_mode) {
        case THERMA_MODE_IDLE:
            if (s_current_deci_c < below) {
                desired = THERMA_MODE_HEATING;
            } else if (s_current_deci_c > above) {
                desired = THERMA_MODE_COOLING;
            }
            break;
        case THERMA_MODE_HEATING:
            if (s_current_deci_c >= setpoint) {
                desired = THERMA_MODE_IDLE;
            }
            break;
        case THERMA_MODE_COOLING:
            if (s_current_deci_c <= setpoint) {
                desired = THERMA_MODE_IDLE;
            }
            break;
        }
    }

    // Minimum dwell: once we've entered a non-idle state, stay there
    // for at least THERMA_MIN_DWELL_MS before allowing a switch. The
    // fail-safe-off path (stale sensor) bypasses dwell because safety
    // overrides chatter protection.
    if (!stale && s_mode != THERMA_MODE_IDLE && desired != s_mode) {
        int64_t dwell_ms = (now_us - s_last_mode_change_us) / 1000;
        if (dwell_ms < THERMA_MIN_DWELL_MS) {
            desired = s_mode;
        }
    }

    set_mode_locked(desired);

    // Read relay feedback and flag mismatches against the commanded mode.
    bool heat_fb = relay_io_read_heat_feedback();
    bool cool_fb = relay_io_read_cool_feedback();

    if ((s_mode == THERMA_MODE_HEATING) != heat_fb) {
        fault |= THERMA_FAULT_HEAT_FB_MISMATCH;
    }
    if ((s_mode == THERMA_MODE_COOLING) != cool_fb) {
        fault |= THERMA_FAULT_COOL_FB_MISMATCH;
    }

    if (out) {
        out->mode = s_mode;
        out->current_deci_c = s_current_deci_c;
        out->heat_fb = heat_fb;
        out->cool_fb = cool_fb;
        out->fault = fault;
        out->current_valid = s_current_valid && !stale;
    }
}
