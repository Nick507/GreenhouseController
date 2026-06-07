#pragma once

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

esp_err_t battery_init(void);
adc_oneshot_unit_handle_t adc_shared_handle(void);
esp_err_t battery_read_mv(int *voltage_mv);
esp_err_t battery_read_percent(int *percent);
