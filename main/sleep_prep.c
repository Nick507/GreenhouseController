#include "sleep_prep.h"

#include "configuration.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "powerCtrl.h"

static const char *TAG = "sleep";

void sleep_release_i2c_pins(void)
{
    gpio_hold_dis(PIN_I2C_SDA);
    gpio_hold_dis(PIN_I2C_SCL);
}

static void gpio_isolate_input(int gpio)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

void sleep_enter_deep(void)
{
    power_ctrl_hold_off();

    gpio_isolate_input(PIN_I2C_SDA);
    gpio_isolate_input(PIN_I2C_SCL);

    esp_deep_sleep_disable_rom_logging();
    gpio_deep_sleep_hold_en();

    uint64_t us = (uint64_t)CONFIG_SLEEP_INTERVAL_MIN * 60ULL * 1000000ULL;
    ESP_LOGI(TAG, "deep sleep %d min", CONFIG_SLEEP_INTERVAL_MIN);
    esp_sleep_enable_timer_wakeup(us);
    esp_deep_sleep_start();
}
