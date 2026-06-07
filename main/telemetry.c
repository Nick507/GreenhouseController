#include "telemetry.h"

#include <stdio.h>
#include <string.h>

#include "battery.h"
#include "bmp280.h"
#include "ds18b20.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "soilMoisture.h"
#include "valve.h"

static const char *TAG = "telemetry";

RTC_DATA_ATTR static uint8_t s_alarm;

static int append_ds18b20_json(char *buf, size_t buf_size, int offset, const telemetry_snapshot_t *snap)
{
    int n = snprintf(buf + offset, buf_size - (size_t)offset, ",\"ds18b20\":{");
    if (n < 0 || offset + n >= (int)buf_size) {
        return -1;
    }
    offset += n;

    for (size_t i = 0; i < snap->ds18b20_count; i++) {
        char rom_str[DS18B20_ROM_STR_LEN];
        ds18b20_rom_to_str(snap->ds18b20[i].rom, rom_str, sizeof(rom_str));

        n = snprintf(buf + offset, buf_size - (size_t)offset,
                     "%s\"%s\":%.1f",
                     i == 0 ? "" : ",",
                     rom_str, snap->ds18b20[i].temp_c);
        if (n < 0 || offset + n >= (int)buf_size) {
            return -1;
        }
        offset += n;
    }

    n = snprintf(buf + offset, buf_size - (size_t)offset, "}");
    if (n < 0 || offset + n >= (int)buf_size) {
        return -1;
    }

    return offset + n;
}

static bool parse_valve_command(const char *json, bool *open)
{
    const char *key = strstr(json, "\"valve\"");
    if (key == NULL) {
        key = strstr(json, "'valve'");
    }
    if (key == NULL) {
        return false;
    }

    const char *colon = strchr(key, ':');
    if (colon == NULL) {
        return false;
    }

    const char *p = colon + 1;
    while (*p == ' ' || *p == '\t' || *p == '"') {
        p++;
    }

    *open = (*p == '1');
    return true;
}

void telemetry_collect(telemetry_snapshot_t *snap)
{
    memset(snap, 0, sizeof(*snap));

    if (battery_read_mv(&snap->battery_mv) != ESP_OK ||
        battery_read_percent(&snap->battery_pct) != ESP_OK) {
        ESP_LOGW(TAG, "battery read failed");
    }

    snap->bmp_ok = (bmp280_read(&snap->bmp) == ESP_OK);
    if (!snap->bmp_ok) {
        ESP_LOGW(TAG, "bmp280 read failed");
    }

    if (ds18b20_read_all(snap->ds18b20, DS18B20_MAX_DEVICES, &snap->ds18b20_count) != ESP_OK) {
        ESP_LOGW(TAG, "ds18b20 read failed");
    }

    if (soil_moisture_read_raw(&snap->soil_raw) != ESP_OK ||
        soil_moisture_read_percent(&snap->soil_pct) != ESP_OK) {
        ESP_LOGW(TAG, "soil moisture read failed");
    }

    snap->valve_open = valve_is_open();
    snap->alarm = s_alarm ? 1 : 0;
}

bool telemetry_build_json(const telemetry_snapshot_t *snap, char *buf, size_t buf_size)
{
    int n;

    if (snap->bmp_ok && snap->bmp.has_humidity) {
        n = snprintf(buf, buf_size,
                     "{\"battery_mv\":%d,\"battery_pct\":%d,"
                     "\"bmp280\":{\"temp_c\":%.2f,\"pressure_hpa\":%.2f,\"humidity_pct\":%.1f},"
                     "\"soil_raw\":%d,\"soil_pct\":%d,\"valve\":%d,\"alarm\":%d",
                     snap->battery_mv, snap->battery_pct,
                     snap->bmp.temp_c, snap->bmp.pressure_hpa, snap->bmp.humidity_pct,
                     snap->soil_raw, snap->soil_pct, snap->valve_open ? 1 : 0, snap->alarm);
    } else if (snap->bmp_ok) {
        n = snprintf(buf, buf_size,
                     "{\"battery_mv\":%d,\"battery_pct\":%d,"
                     "\"bmp280\":{\"temp_c\":%.2f,\"pressure_hpa\":%.2f},"
                     "\"soil_raw\":%d,\"soil_pct\":%d,\"valve\":%d,\"alarm\":%d",
                     snap->battery_mv, snap->battery_pct,
                     snap->bmp.temp_c, snap->bmp.pressure_hpa,
                     snap->soil_raw, snap->soil_pct, snap->valve_open ? 1 : 0, snap->alarm);
    } else {
        n = snprintf(buf, buf_size,
                     "{\"battery_mv\":%d,\"battery_pct\":%d,"
                     "\"soil_raw\":%d,\"soil_pct\":%d,\"valve\":%d,\"alarm\":%d",
                     snap->battery_mv, snap->battery_pct,
                     snap->soil_raw, snap->soil_pct, snap->valve_open ? 1 : 0, snap->alarm);
    }

    if (n < 0 || n >= (int)buf_size) {
        return false;
    }

    int offset = append_ds18b20_json(buf, buf_size, n, snap);
    if (offset < 0) {
        return false;
    }

    if (offset + 1 >= (int)buf_size) {
        return false;
    }
    buf[offset++] = '}';
    buf[offset] = '\0';
    return true;
}

void telemetry_apply_host_response(const char *response)
{
    bool valve_open = false;

    if (!parse_valve_command(response, &valve_open)) {
        ESP_LOGW(TAG, "host response missing valve field: %s", response);
        return;
    }

    ESP_LOGI(TAG, "host valve command: %d", valve_open ? 1 : 0);
    esp_err_t err = valve_set(valve_open);
    if (err != ESP_OK) {
        s_alarm = 1;
        ESP_LOGW(TAG, "valve_set failed, alarm=1: %s", esp_err_to_name(err));
    } else {
        s_alarm = 0;
        ESP_LOGI(TAG, "valve_set ok, alarm cleared");
    }
}
