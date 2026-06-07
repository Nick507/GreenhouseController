#include "ds18b20.h"

#include <stdio.h>

#include "configuration.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ds18b20";

#define CMD_SKIP_ROM        0xCC
#define CMD_CONVERT_T       0x44
#define CMD_READ_SCRATCHPAD 0xBE
#define CMD_MATCH_ROM       0x55
#define CMD_SEARCH_ROM      0xF0

static gpio_num_t bus_pin;
static uint64_t device_roms[DS18B20_MAX_DEVICES];
static size_t device_count;

static uint8_t search_last_discrepancy;
static bool search_last_device;

static void bus_output(void)
{
    gpio_set_direction(bus_pin, GPIO_MODE_OUTPUT);
}

static void bus_input(void)
{
    gpio_set_direction(bus_pin, GPIO_MODE_INPUT);
}

static void bus_low(void)
{
    bus_output();
    gpio_set_level(bus_pin, 0);
}

static void bus_release(void)
{
    bus_input();
}

static bool onewire_reset(void)
{
    bool presence;

    bus_low();
    esp_rom_delay_us(480);
    bus_release();
    esp_rom_delay_us(70);

    presence = (gpio_get_level(bus_pin) == 0);
    esp_rom_delay_us(410);

    return presence;
}

static void onewire_write_bit(int bit)
{
    bus_low();
    if (bit) {
        esp_rom_delay_us(6);
        bus_release();
        esp_rom_delay_us(64);
    } else {
        esp_rom_delay_us(60);
        bus_release();
        esp_rom_delay_us(10);
    }
}

static int onewire_read_bit(void)
{
    int bit;

    bus_low();
    esp_rom_delay_us(6);
    bus_release();
    esp_rom_delay_us(9);
    bit = gpio_get_level(bus_pin);
    esp_rom_delay_us(55);

    return bit;
}

static void onewire_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        onewire_write_bit(byte & 0x01);
        byte >>= 1;
    }
}

static uint8_t onewire_read_byte(void)
{
    uint8_t byte = 0;

    for (int i = 0; i < 8; i++) {
        byte >>= 1;
        if (onewire_read_bit()) {
            byte |= 0x80;
        }
    }

    return byte;
}

static void search_reset(void)
{
    search_last_discrepancy = 0;
    search_last_device = false;
}

static bool onewire_search(uint64_t *rom)
{
    uint64_t search_rom = 0;
    uint8_t discrepancy = 0;

    if (search_last_device) {
        return false;
    }

    if (!onewire_reset()) {
        search_reset();
        return false;
    }

    onewire_write_byte(CMD_SEARCH_ROM);

    for (int i = 0; i < 64; i++) {
        int bit_a = onewire_read_bit();
        int bit_b = onewire_read_bit();

        if (bit_a && bit_b) {
            search_reset();
            return false;
        }

        int direction;
        if (bit_a != bit_b) {
            direction = bit_a;
        } else {
            if (i < search_last_discrepancy) {
                direction = (search_rom >> i) & 0x01;
            } else if (i == search_last_discrepancy) {
                direction = 1;
            } else {
                direction = 0;
            }

            if (direction == 0) {
                discrepancy = i;
            }
        }

        onewire_write_bit(direction);
        search_rom |= ((uint64_t)direction << i);
    }

    if (discrepancy == 0) {
        search_last_device = true;
    }

    search_last_discrepancy = discrepancy;
    *rom = search_rom;
    return true;
}

static void onewire_select(uint64_t rom)
{
    onewire_write_byte(CMD_MATCH_ROM);
    for (int i = 0; i < 8; i++) {
        onewire_write_byte((uint8_t)(rom >> (8 * i)));
    }
}

static esp_err_t start_conversion_all(void)
{
    if (!onewire_reset()) {
        return ESP_ERR_NOT_FOUND;
    }

    onewire_write_byte(CMD_SKIP_ROM);
    onewire_write_byte(CMD_CONVERT_T);
    return ESP_OK;
}

static esp_err_t read_temperature(uint64_t rom, float *temp_c)
{
    if (!onewire_reset()) {
        return ESP_ERR_NOT_FOUND;
    }

    onewire_select(rom);
    onewire_write_byte(CMD_READ_SCRATCHPAD);

    uint8_t scratch[9];
    for (int i = 0; i < 9; i++) {
        scratch[i] = onewire_read_byte();
    }

    int16_t raw = (int16_t)(scratch[1] << 8) | scratch[0];
    *temp_c = (float)raw / 16.0f;
    return ESP_OK;
}

static esp_err_t scan_devices(void)
{
    uint64_t rom;
    size_t count = 0;

    search_reset();

    while (count < DS18B20_MAX_DEVICES && onewire_search(&rom)) {
        if ((uint8_t)rom != 0x28) {
            continue;
        }

        device_roms[count++] = rom;
        char rom_str[DS18B20_ROM_STR_LEN];
        ds18b20_rom_to_str(rom, rom_str, sizeof(rom_str));
        ESP_LOGI(TAG, "found device %s", rom_str);
    }

    device_count = count;
    ESP_LOGI(TAG, "%u device(s) on bus", (unsigned)device_count);
    return count > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void ds18b20_rom_to_str(uint64_t rom, char *out, size_t out_len)
{
    const uint8_t *b = (const uint8_t *)&rom;

    if (out == NULL || out_len < DS18B20_ROM_STR_LEN) {
        return;
    }

    snprintf(out, out_len, "%02X%02X%02X%02X%02X%02X%02X%02X",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
}

esp_err_t ds18b20_init(void)
{
    bus_pin = PIN_DS18B20;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << bus_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    return scan_devices();
}

esp_err_t ds18b20_read_all(ds18b20_reading_t *readings, size_t max_count, size_t *count)
{
    if (readings == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (device_count == 0) {
        *count = 0;
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = start_conversion_all();
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(750));

    size_t out = 0;
    for (size_t i = 0; i < device_count && out < max_count; i++) {
        readings[out].rom = device_roms[i];
        err = read_temperature(device_roms[i], &readings[out].temp_c);
        if (err == ESP_OK) {
            out++;
        } else {
            char rom_str[DS18B20_ROM_STR_LEN];
            ds18b20_rom_to_str(device_roms[i], rom_str, sizeof(rom_str));
            ESP_LOGW(TAG, "read failed for %s", rom_str);
        }
    }

    *count = out;
    return out > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}
