#include "can_handler.h"
#include "board.h"
#include "thermostat.h"
#include "state_store.h"
#include "wifi_config.h"
#include "discovery.h"
#include "ota.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "can";

#define TICK_INTERVAL_MS       1000   // 1 Hz periodic broadcast
#define TX_PROBE_INTERVAL_MS   2000   // slow probe when no peers detected

esp_err_t can_handler_init(void)
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t ret = twai_driver_install(&g_config, &t_config, &f_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TWAI driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = twai_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "CAN bus initialized (500 kbps, NORMAL, TX=%d RX=%d)",
             CAN_TX_PIN, CAN_RX_PIN);
    return ESP_OK;
}

static void broadcast_version(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    const esp_app_desc_t *app = esp_app_get_description();
    unsigned maj = 0, min = 0, pat = 0;
    sscanf(app->version, "%u.%u.%u", &maj, &min, &pat);
    twai_message_t msg = {
        .identifier = CAN_ID_VERSION,
        .data_length_code = 6,
        .data = { mac[3], mac[4], mac[5], maj, min, pat },
    };
    twai_transmit(&msg, pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "Version broadcast: %s (CAN 0x%02X)", app->version, CAN_ID_VERSION);
}

static void broadcast_desired(void)
{
    int16_t sp = state_store_get_setpoint_deci_c();
    uint8_t th = state_store_get_threshold_deci_c();
    // 16-bit values on the TrailCurrent CAN bus are big-endian (Motorola).
    twai_message_t msg = {
        .identifier = CAN_ID_THERMA_DESIRED,
        .data_length_code = 3,
        .data = {
            (uint8_t)((sp >> 8) & 0xFF),
            (uint8_t)(sp & 0xFF),
            th,
        },
    };
    twai_transmit(&msg, 0);
}

static void broadcast_status(const therma_status_t *st)
{
    twai_message_t msg = {
        .identifier = CAN_ID_THERMA_STATUS,
        .data_length_code = 6,
        .data = {
            (uint8_t)st->mode,
            (uint8_t)((st->current_deci_c >> 8) & 0xFF),
            (uint8_t)(st->current_deci_c & 0xFF),
            st->heat_fb ? 1 : 0,
            st->cool_fb ? 1 : 0,
            st->fault,
        },
    };
    twai_transmit(&msg, 0);
}

static void handle_borealis_environment(const uint8_t *data, uint8_t len)
{
    if (len < 1) return;
    // Borealis broadcasts temperature in whole degrees C (signed int8).
    // Normalize to 0.1 C units so the thermostat can use it directly.
    int8_t whole = (int8_t)data[0];
    int16_t deci_c = (int16_t)whole * TEMP_SCALE;
    thermostat_on_current_temperature(deci_c);
}

static void handle_set_desired(const uint8_t *data, uint8_t len)
{
    if (len < 2) return;
    int16_t setpoint = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
    if (state_store_set_setpoint_deci_c(setpoint)) {
        // Immediately echo the new authoritative state.
        broadcast_desired();
    }
}

static void handle_set_threshold(const uint8_t *data, uint8_t len)
{
    if (len < 1) return;
    if (state_store_set_threshold_deci_c(data[0])) {
        broadcast_desired();
    }
}

void can_handler_task(void *arg)
{
    uint32_t alerts = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS |
                      TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL |
                      TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED |
                      TWAI_ALERT_ERR_ACTIVE | TWAI_ALERT_TX_FAILED |
                      TWAI_ALERT_TX_SUCCESS;
    twai_reconfigure_alerts(alerts, NULL);

    broadcast_version();

    typedef enum { TX_ACTIVE, TX_PROBING } tx_state_t;
    bool bus_off = false;
    tx_state_t tx_state = TX_ACTIVE;
    int tx_fail_count = 0;
    const int TX_FAIL_THRESHOLD = 3;
    int64_t last_tick_us = 0;
    const int64_t tick_period_us = TICK_INTERVAL_MS * 1000LL;
    const int64_t tx_probe_period_us = TX_PROBE_INTERVAL_MS * 1000LL;

    while (1) {
        uint32_t triggered;
        twai_read_alerts(&triggered, pdMS_TO_TICKS(50));

        // --- Bus error handling ---
        if (triggered & TWAI_ALERT_BUS_OFF) {
            ESP_LOGE(TAG, "TWAI bus-off, initiating recovery");
            bus_off = true;
            twai_initiate_recovery();
            // Drop outputs on bus-off; we lose our truth broadcast.
            thermostat_tick(NULL);  // not strictly needed, but keeps state fresh
            continue;
        }
        if (triggered & TWAI_ALERT_BUS_RECOVERED) {
            ESP_LOGI(TAG, "TWAI bus recovered, restarting");
            twai_start();
            bus_off = false;
            tx_fail_count = 0;
            tx_state = TX_PROBING;
        }
        if (triggered & TWAI_ALERT_ERR_PASS) {
            ESP_LOGW(TAG, "TWAI error passive (no peers ACKing?)");
        }
        if (triggered & TWAI_ALERT_TX_FAILED) {
            if (tx_state == TX_ACTIVE) {
                tx_fail_count++;
                if (tx_fail_count >= TX_FAIL_THRESHOLD) {
                    tx_state = TX_PROBING;
                    ESP_LOGW(TAG, "TWAI no peers detected, entering slow probe");
                }
            }
        }
        if (triggered & TWAI_ALERT_TX_SUCCESS) {
            if (tx_state == TX_PROBING) {
                tx_state = TX_ACTIVE;
                tx_fail_count = 0;
                ESP_LOGI(TAG, "TWAI probe ACK'd, peer detected, resuming normal TX");
            }
            tx_fail_count = 0;
        }

        // --- Drain received messages ---
        if (triggered & TWAI_ALERT_RX_DATA) {
            if (tx_state == TX_PROBING) {
                tx_state = TX_ACTIVE;
                tx_fail_count = 0;
                ESP_LOGI(TAG, "TWAI peer detected via RX, resuming normal TX");
            }
            twai_message_t msg;
            while (twai_receive(&msg, 0) == ESP_OK) {
                if (msg.rtr) continue;

                switch (msg.identifier) {
                case CAN_ID_OTA:
                    if (msg.data_length_code >= 3) {
                        ota_handle_trigger(msg.data, msg.data_length_code);
                    }
                    break;
                case CAN_ID_WIFI_CONFIG:
                    if (msg.data_length_code >= 1) {
                        wifi_config_handle_can(msg.data, msg.data_length_code);
                    }
                    break;
                case CAN_ID_DISCOVERY_TRIGGER:
                    discovery_handle_trigger();
                    break;
                case CAN_ID_BOREALIS_ENVIRONMENT:
                    handle_borealis_environment(msg.data, msg.data_length_code);
                    break;
                case CAN_ID_THERMA_SET_DESIRED:
                    handle_set_desired(msg.data, msg.data_length_code);
                    break;
                case CAN_ID_THERMA_SET_THRESHOLD:
                    handle_set_threshold(msg.data, msg.data_length_code);
                    break;
                default:
                    break;
                }
            }
        }

        // Check wifi config timeout
        wifi_config_check_timeout();

        // --- Periodic tick: run control loop + authoritative broadcasts ---
        int64_t now = esp_timer_get_time();
        int64_t effective_period = (tx_state == TX_PROBING) ? tx_probe_period_us : tick_period_us;
        if (!bus_off && (now - last_tick_us >= effective_period)) {
            last_tick_us = now;

            therma_status_t st;
            thermostat_tick(&st);
            broadcast_desired();
            broadcast_status(&st);
        }
    }
}
