#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define DS18B20_MAX_DEVICES 8

typedef struct {
    uint64_t rom;
    float temp_c;
} ds18b20_reading_t;

#define DS18B20_ROM_STR_LEN 17

esp_err_t ds18b20_init(void);
esp_err_t ds18b20_read_all(ds18b20_reading_t *readings, size_t max_count, size_t *count);
void ds18b20_rom_to_str(uint64_t rom, char *out, size_t out_len);
