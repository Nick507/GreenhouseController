#include "soilMoisture.h"

#include "battery.h"
#include "configuration.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "soil";

static adc_channel_t s_soil_channel;
static bool s_soil_ready;

esp_err_t soil_moisture_init(void)
{
    adc_unit_t unit = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(PIN_SOIL_MOISTURE_ADC, &unit, &s_soil_channel));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_shared_handle(), s_soil_channel, &chan_cfg));

    s_soil_ready = true;
    ESP_LOGI(TAG, "ADC on GPIO%d (unit %d ch %d)", PIN_SOIL_MOISTURE_ADC, unit, s_soil_channel);
    return ESP_OK;
}

esp_err_t soil_moisture_read_raw(int *raw)
{
    if (raw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_soil_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_ERROR_CHECK(adc_oneshot_read(adc_shared_handle(), s_soil_channel, raw));
    return ESP_OK;
}

esp_err_t soil_moisture_read_percent(int *percent)
{
    if (percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int raw = 0;
    esp_err_t err = soil_moisture_read_raw(&raw);
    if (err != ESP_OK) {
        return err;
    }
    
    int battery_mv = 0;
    err = battery_read_mv(&battery_mv);
    if (err != ESP_OK) {
        return err;
    }

    if (battery_mv < 2800 || battery_mv > 3400)
    {
        ESP_LOGW(TAG, "battery voltage out of range: %d mV", battery_mv);
        return ESP_ERR_INVALID_STATE;
    }

    int adjusted_min = (int)((long)SOIL_ADC_MIN * battery_mv / SOIL_BATTERY_CALIBRATION_LEVEL);
    int adjusted_max = (int)((long)SOIL_ADC_MAX * battery_mv / SOIL_BATTERY_CALIBRATION_LEVEL);

    *percent = 100 - (raw - adjusted_min) * 100 / (adjusted_max - adjusted_min);

    return ESP_OK;
}
