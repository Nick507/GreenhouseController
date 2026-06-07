#pragma once

#include "driver/gpio.h"

// ============================================================================
// ESP32-C3 SUPERMINI GREENHOUSE CONTROLLER PIN CONFIGURATION
// ============================================================================

// --- I2C Communication Bus ---
#define PIN_I2C_SDA             5
#define PIN_I2C_SCL             6

// --- Switched Power Control (High-Side Switch) ---
#define PIN_PWR_CTRL            4

// --- Actuator & Feedback Control (Valve) ---
#define PIN_VALVE_MOTOR         10
#define PIN_VALVE_STATE         7

// --- 1-Wire Temperature Bus ---
#define PIN_DS18B20             3

// --- Analog Inputs (ADC) ---
#define PIN_PWR_ADC             0
#define PIN_SOIL_MOISTURE_ADC   1

// ============================================================================
// HARDWARE LOGIC LEVELS
// ============================================================================
#define PERIPHERAL_POWER_ON     0
#define PERIPHERAL_POWER_OFF    1

#define VALVE_OPEN              0
#define VALVE_CLOSE             1

// ============================================================================
// TIMING
// ============================================================================
#define PERIPHERAL_POWER_ON_MS      500
#define VALVE_TIMEOUT_MS            5000
#define SENSOR_POLL_INTERVAL_MS     5000
#define CONFIG_SLEEP_INTERVAL_MIN 10

// ============================================================================
// BATTERY ADC (BAT - 47K - PWR_ADC - 100K - GND)
// Vbat = Vadc * (R1 + R2) / R2
// ============================================================================
#define BATTERY_DIVIDER_R1_OHMS     47000
#define BATTERY_DIVIDER_R2_OHMS     100000
#define BATTERY_DIVIDER_RATIO       ((float)(BATTERY_DIVIDER_R1_OHMS + BATTERY_DIVIDER_R2_OHMS) / (float)BATTERY_DIVIDER_R2_OHMS)

// ============================================================================
// SOIL MOISTURE ADC (sensor - 22K - ADC - 100K - GND)
// Testing: 0 raw = 0%, max raw = 100%. Calibrate endpoints later.
// ============================================================================
#define SOIL_DIVIDER_R1_OHMS        22000
#define SOIL_DIVIDER_R2_OHMS        100000
#define SOIL_ADC_MAX_RAW            4095

