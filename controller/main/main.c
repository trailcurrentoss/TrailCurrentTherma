#include "board.h"
#include "can_handler.h"
#include "relay_io.h"
#include "thermostat.h"
#include "state_store.h"
#include "wifi_config.h"
#include "discovery.h"
#include "ota.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "=== TrailCurrent Therma Controller ===");
    ESP_LOGI(TAG, "Board: Waveshare ESP32-S3-RS485-CAN");

    // Initialize NVS (also used by state_store for the thermostat values)
    // and load WiFi credentials.
    ESP_ERROR_CHECK(wifi_config_init());

    char ssid[33] = {0};
    char password[64] = {0};
    if (wifi_config_load(ssid, sizeof(ssid), password, sizeof(password))) {
        ESP_LOGI(TAG, "WiFi credentials loaded from NVS");
    } else {
        ESP_LOGI(TAG, "No WiFi credentials — OTA disabled until provisioned via CAN");
    }

    ESP_ERROR_CHECK(state_store_init());
    ESP_ERROR_CHECK(relay_io_init());
    thermostat_init();

    discovery_init();
    ota_init();

    ESP_ERROR_CHECK(can_handler_init());

    ESP_LOGI(TAG, "=== Setup Complete ===");

    // The CAN task runs the periodic thermostat tick on its 1 Hz ticker
    // so all outputs and broadcasts stay synchronized.
    xTaskCreatePinnedToCore(can_handler_task, "can_task", 4096, NULL, 5, NULL, 1);
}
