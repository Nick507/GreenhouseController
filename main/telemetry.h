#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bmp280.h"
#include "ds18b20.h"

#define TELEMETRY_JSON_SIZE     1024
#define TELEMETRY_RESPONSE_SIZE 128

typedef struct {
    int battery_mv;
    int battery_pct;
    bool bmp_ok;
    bmp280_reading_t bmp;
    ds18b20_reading_t ds18b20[DS18B20_MAX_DEVICES];
    size_t ds18b20_count;
    int soil_raw;
    int soil_pct;
    bool valve_open;
    int alarm;
} telemetry_snapshot_t;

void telemetry_collect(telemetry_snapshot_t *snap);
bool telemetry_build_json(const telemetry_snapshot_t *snap, char *buf, size_t buf_size);
void telemetry_apply_host_response(const char *response);
