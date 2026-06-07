#include "soilMoisture.h"

#include "battery.h"
#include "configuration.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "soil";

esp_err_t soil_moisture_init(void)
{
    ESP_LOGI(TAG, "ADC on GPIO%d", PIN_SOIL_MOISTURE_ADC);
    return ESP_OK;
}

esp_err_t soil_moisture_read_raw(int *raw)
{
    if (raw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(adc_oneshot_read(adc_shared_handle(), ADC_CHANNEL_1, raw));
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

    if (raw <= 0) {
        *percent = 0;
    } else if (raw >= SOIL_ADC_MAX_RAW) {
        *percent = 100;
    } else {
        *percent = (raw * 100) / SOIL_ADC_MAX_RAW;
    }

    return ESP_OK;
}
