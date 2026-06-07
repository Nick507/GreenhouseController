#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    float temp_c;
    float pressure_hpa;
    float humidity_pct;
    bool has_humidity;
} bmp280_reading_t;

esp_err_t bmp280_init(void);
esp_err_t bmp280_read(bmp280_reading_t *reading);
