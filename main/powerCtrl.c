#include "powerCtrl.h"

#include "configuration.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power";

static int power_level(bool on)
{
    return on ? PERIPHERAL_POWER_ON : PERIPHERAL_POWER_OFF;
}

esp_err_t power_ctrl_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_PWR_CTRL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    gpio_hold_dis(PIN_PWR_CTRL);
    power_ctrl_set(false);
    ESP_LOGI(TAG, "peripheral power on GPIO%d (active %s)", PIN_PWR_CTRL,
             PERIPHERAL_POWER_ON ? "LOW" : "HIGH");
    return ESP_OK;
}

void power_ctrl_set(bool on)
{
    gpio_set_level(PIN_PWR_CTRL, power_level(on));
    if (on) {
        vTaskDelay(pdMS_TO_TICKS(PERIPHERAL_POWER_ON_MS));
    }
}

void power_ctrl_hold_off(void)
{
    gpio_set_level(PIN_PWR_CTRL, PERIPHERAL_POWER_OFF);
    gpio_hold_en(PIN_PWR_CTRL);
}
