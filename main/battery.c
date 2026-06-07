#include "battery.h"

#include <stddef.h>

#include "configuration.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "battery";

typedef struct {
    uint16_t voltage_mv;
    uint8_t percentage;
} ocv_map_t;

static const ocv_map_t lifepo4_soc_table[] = {
    {3600, 100},
    {3400, 99},
    {3350, 90},
    {3320, 70},
    {3300, 40},
    {3250, 20},
    {3200, 10},
    {3000, 5},
    {2800, 0},
};

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle;
static adc_channel_t s_battery_channel;
static bool cali_enabled;

static esp_err_t adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten,
                                      adc_cali_handle_t *out)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
#endif

    *out = handle;
    return ret;
}

esp_err_t battery_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_unit_t unit = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(PIN_PWR_ADC, &unit, &s_battery_channel));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, s_battery_channel, &chan_cfg));

    cali_enabled = adc_calibration_init(unit, s_battery_channel, ADC_ATTEN_DB_12, &cali_handle) == ESP_OK;
    ESP_LOGI(TAG, "ADC on GPIO%d, calibration %s", PIN_PWR_ADC, cali_enabled ? "enabled" : "disabled");
    return ESP_OK;
}

adc_oneshot_unit_handle_t adc_shared_handle(void)
{
    return adc_handle;
}

esp_err_t battery_read_mv(int *voltage_mv)
{
    if (voltage_mv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, s_battery_channel, &raw));

    int adc_mv = 0;
    if (cali_enabled) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &adc_mv));
    } else {
        adc_mv = (raw * 3300) / 4095;
    }

    *voltage_mv = (int)(adc_mv * BATTERY_DIVIDER_RATIO);
    return ESP_OK;
}

static int lifepo4_percent_from_mv(int mv)
{
    const size_t count = sizeof(lifepo4_soc_table) / sizeof(lifepo4_soc_table[0]);

    if (mv >= lifepo4_soc_table[0].voltage_mv) {
        return lifepo4_soc_table[0].percentage;
    }
    if (mv <= lifepo4_soc_table[count - 1].voltage_mv) {
        return lifepo4_soc_table[count - 1].percentage;
    }

    for (size_t i = 0; i < count - 1; i++) {
        const ocv_map_t *high = &lifepo4_soc_table[i];
        const ocv_map_t *low = &lifepo4_soc_table[i + 1];

        if (mv <= high->voltage_mv && mv >= low->voltage_mv) {
            int voltage_span = (int)high->voltage_mv - (int)low->voltage_mv;
            int percent_span = (int)low->percentage - (int)high->percentage;
            int offset_mv = (int)high->voltage_mv - mv;

            return (int)high->percentage + (offset_mv * percent_span) / voltage_span;
        }
    }

    return 0;
}

esp_err_t battery_read_percent(int *percent)
{
    if (percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int mv = 0;
    esp_err_t err = battery_read_mv(&mv);
    if (err != ESP_OK) {
        return err;
    }

    *percent = lifepo4_percent_from_mv(mv);
    return ESP_OK;
}
