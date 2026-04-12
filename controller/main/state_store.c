#include "state_store.h"
#include "board.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "state_store";

#define NVS_NAMESPACE        "therma"
#define NVS_KEY_SETPOINT     "setpoint"
#define NVS_KEY_THRESHOLD    "threshold"

static nvs_handle_t s_nvs;
static int16_t s_setpoint_deci_c = THERMA_DEFAULT_SETPOINT_DECI_C;
static uint8_t s_threshold_deci_c = THERMA_DEFAULT_THRESHOLD_DECI_C;

esp_err_t state_store_init(void)
{
    // nvs_flash_init() is already called by wifi_config_init(). We just
    // open our own namespace on top of the shared NVS partition.
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    int16_t sp;
    if (nvs_get_i16(s_nvs, NVS_KEY_SETPOINT, &sp) == ESP_OK) {
        if (sp >= THERMA_MIN_SETPOINT_DECI_C && sp <= THERMA_MAX_SETPOINT_DECI_C) {
            s_setpoint_deci_c = sp;
        }
    }

    uint8_t th;
    if (nvs_get_u8(s_nvs, NVS_KEY_THRESHOLD, &th) == ESP_OK) {
        if (th >= THERMA_MIN_THRESHOLD_DECI_C && th <= THERMA_MAX_THRESHOLD_DECI_C) {
            s_threshold_deci_c = th;
        }
    }

    ESP_LOGI(TAG, "Loaded setpoint=%d.%dC threshold=%d.%dC",
             s_setpoint_deci_c / TEMP_SCALE, s_setpoint_deci_c % TEMP_SCALE,
             s_threshold_deci_c / TEMP_SCALE, s_threshold_deci_c % TEMP_SCALE);
    return ESP_OK;
}

int16_t state_store_get_setpoint_deci_c(void)
{
    return s_setpoint_deci_c;
}

uint8_t state_store_get_threshold_deci_c(void)
{
    return s_threshold_deci_c;
}

bool state_store_set_setpoint_deci_c(int16_t setpoint_deci_c)
{
    if (setpoint_deci_c < THERMA_MIN_SETPOINT_DECI_C ||
        setpoint_deci_c > THERMA_MAX_SETPOINT_DECI_C) {
        ESP_LOGW(TAG, "Rejected setpoint %d (out of range)", setpoint_deci_c);
        return false;
    }
    if (setpoint_deci_c == s_setpoint_deci_c) return true;

    s_setpoint_deci_c = setpoint_deci_c;
    nvs_set_i16(s_nvs, NVS_KEY_SETPOINT, setpoint_deci_c);
    nvs_commit(s_nvs);
    ESP_LOGI(TAG, "Setpoint updated to %d.%dC",
             setpoint_deci_c / TEMP_SCALE, setpoint_deci_c % TEMP_SCALE);
    return true;
}

bool state_store_set_threshold_deci_c(uint8_t threshold_deci_c)
{
    if (threshold_deci_c < THERMA_MIN_THRESHOLD_DECI_C ||
        threshold_deci_c > THERMA_MAX_THRESHOLD_DECI_C) {
        ESP_LOGW(TAG, "Rejected threshold %u (out of range)", threshold_deci_c);
        return false;
    }
    if (threshold_deci_c == s_threshold_deci_c) return true;

    s_threshold_deci_c = threshold_deci_c;
    nvs_set_u8(s_nvs, NVS_KEY_THRESHOLD, threshold_deci_c);
    nvs_commit(s_nvs);
    ESP_LOGI(TAG, "Threshold updated to %u.%uC",
             threshold_deci_c / TEMP_SCALE, threshold_deci_c % TEMP_SCALE);
    return true;
}
