// TrailCurrent Therma Relay firmware.
//
// Target: Waveshare ESP32-S3-Relay-1CH.
//   GPIO47  — onboard relay drive (fixed by the board)
//   GPIO3   — CMD_IN (from controller, 3V3 logic, internal pulldown)
//   GPIO4   — STATUS_OUT (back to controller, mirrors commanded state)
//
// Two build variants produced from this same source:
//   -DTHERMA_RELAY_ROLE=HEATER  -> therma_heater_relay.bin
//   -DTHERMA_RELAY_ROLE=COOLER  -> therma_cooler_relay.bin
// The role flag only affects log tag / version banner — the GPIO
// behavior is identical in both artifacts. The semantic difference
// (heater vs cooler) lives entirely in how the controller wires the
// CMD_OUT pins on each board.

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RELAY_DRIVE_PIN     GPIO_NUM_47
#define CMD_IN_PIN          GPIO_NUM_3
#define STATUS_OUT_PIN      GPIO_NUM_4

// Debounce the command input to shrug off line bounce / transients.
#define DEBOUNCE_SAMPLES    3
#define LOOP_PERIOD_MS      10

#if defined(THERMA_RELAY_ROLE_HEATER)
#  define ROLE_NAME "heater"
#elif defined(THERMA_RELAY_ROLE_COOLER)
#  define ROLE_NAME "cooler"
#else
#  define ROLE_NAME "unconfigured"
#endif

static const char *TAG = "therma_" ROLE_NAME "_relay";

static void init_gpio(void)
{
    gpio_config_t out_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << RELAY_DRIVE_PIN) | (1ULL << STATUS_OUT_PIN),
    };
    gpio_config(&out_conf);
    gpio_set_level(RELAY_DRIVE_PIN, 0);
    gpio_set_level(STATUS_OUT_PIN, 0);

    gpio_config_t in_conf = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << CMD_IN_PIN),
    };
    gpio_config(&in_conf);
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "=== TrailCurrent Therma Relay (%s) ===", ROLE_NAME);
    ESP_LOGI(TAG, "Firmware version: %s", app->version);
    ESP_LOGI(TAG, "Relay=GPIO%d  CMD_IN=GPIO%d  STATUS_OUT=GPIO%d",
             RELAY_DRIVE_PIN, CMD_IN_PIN, STATUS_OUT_PIN);

    init_gpio();

    bool relay_state = false;
    int consecutive_same = 0;
    int last_sample = 0;

    while (1) {
        int sample = gpio_get_level(CMD_IN_PIN);
        if (sample == last_sample) {
            if (consecutive_same < DEBOUNCE_SAMPLES) consecutive_same++;
        } else {
            consecutive_same = 1;
            last_sample = sample;
        }

        if (consecutive_same >= DEBOUNCE_SAMPLES) {
            bool desired = (sample != 0);
            if (desired != relay_state) {
                relay_state = desired;
                gpio_set_level(RELAY_DRIVE_PIN, relay_state ? 1 : 0);
                gpio_set_level(STATUS_OUT_PIN, relay_state ? 1 : 0);
                ESP_LOGI(TAG, "Relay %s", relay_state ? "ON" : "OFF");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_PERIOD_MS));
    }
}
