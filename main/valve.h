#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t valve_init(void);
esp_err_t valve_set(bool open);
bool valve_is_open(void);
