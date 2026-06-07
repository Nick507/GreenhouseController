#pragma once

#include "esp_err.h"

esp_err_t soil_moisture_init(void);
esp_err_t soil_moisture_read_raw(int *raw);
esp_err_t soil_moisture_read_percent(int *percent);
