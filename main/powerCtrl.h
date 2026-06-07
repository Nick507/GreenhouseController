#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t power_ctrl_init(void);
void power_ctrl_set(bool on);
void power_ctrl_hold_off(void);
