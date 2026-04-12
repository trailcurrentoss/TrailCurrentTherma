#include "relay_io.h"
#include "board.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "relay_io";

static therma_mode_t s_mode = THERMA_MODE_IDLE;

esp_err_t relay_io_init(void)
{
    gpio_config_t out_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << HEAT_CMD_OUT_PIN) | (1ULL << COOL_CMD_OUT_PIN),
    };
    gpio_config(&out_conf);

    gpio_config_t in_conf = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << HEAT_STATUS_IN_PIN) | (1ULL << COOL_STATUS_IN_PIN),
    };
    gpio_config(&in_conf);

    // Boot fail-safe: both outputs off.
    gpio_set_level(HEAT_CMD_OUT_PIN, 0);
    gpio_set_level(COOL_CMD_OUT_PIN, 0);
    s_mode = THERMA_MODE_IDLE;

    ESP_LOGI(TAG, "Relay GPIOs initialized (HEAT_CMD=%d HEAT_FB=%d COOL_CMD=%d COOL_FB=%d)",
             HEAT_CMD_OUT_PIN, HEAT_STATUS_IN_PIN, COOL_CMD_OUT_PIN, COOL_STATUS_IN_PIN);
    return ESP_OK;
}

void relay_io_set_mode(therma_mode_t mode)
{
    if (mode == s_mode) return;

    // Always drop both before raising the new one. This guarantees
    // mutual exclusion under any scheduling jitter.
    gpio_set_level(HEAT_CMD_OUT_PIN, 0);
    gpio_set_level(COOL_CMD_OUT_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(THERMA_SWAP_DEAD_TIME_MS));

    switch (mode) {
    case THERMA_MODE_HEATING:
        gpio_set_level(HEAT_CMD_OUT_PIN, 1);
        break;
    case THERMA_MODE_COOLING:
        gpio_set_level(COOL_CMD_OUT_PIN, 1);
        break;
    case THERMA_MODE_IDLE:
    default:
        break;
    }
    s_mode = mode;

    ESP_LOGI(TAG, "Mode -> %s",
             mode == THERMA_MODE_HEATING ? "HEATING" :
             mode == THERMA_MODE_COOLING ? "COOLING" : "IDLE");
}

therma_mode_t relay_io_get_commanded_mode(void)
{
    return s_mode;
}

bool relay_io_read_heat_feedback(void)
{
    return gpio_get_level(HEAT_STATUS_IN_PIN) != 0;
}

bool relay_io_read_cool_feedback(void)
{
    return gpio_get_level(COOL_STATUS_IN_PIN) != 0;
}
