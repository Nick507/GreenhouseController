#include "bmp280.h"

#include <stdio.h>
#include <string.h>

#include "configuration.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bmp280";

#define I2C_FREQ_HZ         100000

#define REG_CHIP_ID         0xD0
#define REG_RESET           0xE0
#define REG_CTRL_HUM        0xF2
#define REG_STATUS          0xF3
#define REG_CTRL_MEAS       0xF4
#define REG_CONFIG          0xF5
#define REG_PRESS_MSB       0xF7

#define CHIP_ID_BMP280      0x58
#define CHIP_ID_BME280      0x60

#define ADDR_PRIMARY        0x76
#define ADDR_SECONDARY      0x77

typedef enum {
    SENSOR_NONE = 0,
    SENSOR_BMP280,
    SENSOR_BME280,
} sensor_type_t;

typedef struct {
    sensor_type_t type;
    uint8_t i2c_addr;
    i2c_master_dev_handle_t dev;

    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;

    uint8_t dig_H1;
    int16_t dig_H2;
    int16_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;

    int32_t t_fine;
} sensor_t;

static i2c_master_bus_handle_t bus;
static sensor_t sensor;
static bool initialized;

static esp_err_t reg_read(sensor_t *s, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s->dev, &reg, 1, data, len, pdMS_TO_TICKS(1000));
}

static esp_err_t reg_write(sensor_t *s, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s->dev, buf, sizeof(buf), pdMS_TO_TICKS(1000));
}

static esp_err_t load_calibration(sensor_t *s)
{
    uint8_t cal[26];
    esp_err_t err = reg_read(s, 0x88, cal, sizeof(cal));
    if (err != ESP_OK) {
        return err;
    }

    s->dig_T1 = (uint16_t)cal[0] | ((uint16_t)cal[1] << 8);
    s->dig_T2 = (int16_t)(cal[2] | (cal[3] << 8));
    s->dig_T3 = (int16_t)(cal[4] | (cal[5] << 8));
    s->dig_P1 = (uint16_t)cal[6] | ((uint16_t)cal[7] << 8);
    s->dig_P2 = (int16_t)(cal[8] | (cal[9] << 8));
    s->dig_P3 = (int16_t)(cal[10] | (cal[11] << 8));
    s->dig_P4 = (int16_t)(cal[12] | (cal[13] << 8));
    s->dig_P5 = (int16_t)(cal[14] | (cal[15] << 8));
    s->dig_P6 = (int16_t)(cal[16] | (cal[17] << 8));
    s->dig_P7 = (int16_t)(cal[18] | (cal[19] << 8));
    s->dig_P8 = (int16_t)(cal[20] | (cal[21] << 8));
    s->dig_P9 = (int16_t)(cal[22] | (cal[23] << 8));

    if (s->type != SENSOR_BME280) {
        return ESP_OK;
    }

    s->dig_H1 = cal[25];

    uint8_t h2[7];
    err = reg_read(s, 0xE1, h2, sizeof(h2));
    if (err != ESP_OK) {
        return err;
    }

    s->dig_H2 = (int16_t)(h2[0] | (h2[1] << 8));
    s->dig_H3 = (int16_t)(int8_t)h2[2];
    s->dig_H4 = (int16_t)(((int16_t)h2[3]) << 4) | (h2[4] & 0x0F);
    s->dig_H5 = (int16_t)(((int16_t)h2[5]) << 4) | (h2[4] >> 4);
    s->dig_H6 = (int8_t)h2[6];
    if (s->dig_H4 > 0x7FFF) {
        s->dig_H4 -= 0x10000;
    }
    if (s->dig_H5 > 0x7FFF) {
        s->dig_H5 -= 0x10000;
    }

    return ESP_OK;
}

static esp_err_t sensor_measure_forced(sensor_t *s)
{
    esp_err_t err = reg_write(s, REG_RESET, 0xB6);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    if (s->type == SENSOR_BME280) {
        err = reg_write(s, REG_CTRL_HUM, 0x01);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = reg_write(s, REG_CONFIG, 0x00);
    if (err != ESP_OK) {
        return err;
    }

    err = reg_write(s, REG_CTRL_MEAS, 0x25);
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < 50; i++) {
        uint8_t status = 0;
        err = reg_read(s, REG_STATUS, &status, 1);
        if (err != ESP_OK) {
            return err;
        }
        if ((status & 0x08) == 0) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_ERR_TIMEOUT;
}

static float compensate_temperature(sensor_t *s, int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)s->dig_T1 << 1))) * (int32_t)s->dig_T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)s->dig_T1) *
                      ((adc_T >> 4) - (int32_t)s->dig_T1)) >> 12) *
                    (int32_t)s->dig_T3) >> 14;

    s->t_fine = var1 + var2;
    return (float)((s->t_fine * 5 + 128) >> 8) / 100.0f;
}

static float compensate_pressure(sensor_t *s, int32_t adc_P)
{
    int64_t var1 = ((int64_t)s->t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)s->dig_P6;
    var2 = var2 + ((var1 * (int64_t)s->dig_P5) << 17);
    var2 = var2 + (((int64_t)s->dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)s->dig_P3) >> 8) + ((var1 * (int64_t)s->dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * (int64_t)s->dig_P1) >> 33;

    if (var1 == 0) {
        return 0.0f;
    }

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)s->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)s->dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)s->dig_P7) << 4);

    return (float)p / 25600.0f;
}

static float compensate_humidity(sensor_t *s, int32_t adc_H)
{
    int32_t v_x1_u32r;

    v_x1_u32r = (s->t_fine - (int32_t)76800);
    v_x1_u32r = (((((adc_H << 14) - ((int32_t)s->dig_H4 << 20) -
                     ((int32_t)s->dig_H5 * v_x1_u32r)) +
                    ((int32_t)16384)) >> 15) *
                 (((((((v_x1_u32r * (int32_t)s->dig_H6) >> 10) *
                      (((v_x1_u32r * (int32_t)s->dig_H3) >> 11) + ((int32_t)32768))) >>
                     10) +
                    ((int32_t)2097152)) * (int32_t)s->dig_H2 + 8192) >> 14));

    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                               (int32_t)s->dig_H1) >> 4));

    if (v_x1_u32r < 0) {
        v_x1_u32r = 0;
    }
    if (v_x1_u32r > 419430400) {
        v_x1_u32r = 419430400;
    }

    return (float)(v_x1_u32r >> 12) / 1024.0f;
}

static esp_err_t read_raw(sensor_t *s, int32_t *press, int32_t *temp, int32_t *hum)
{
    uint8_t data[8];
    esp_err_t err = reg_read(s, REG_PRESS_MSB, data, s->type == SENSOR_BME280 ? 8 : 6);
    if (err != ESP_OK) {
        return err;
    }

    *press = (int32_t)((data[0] << 12) | (data[1] << 4) | (data[2] >> 4));
    *temp = (int32_t)((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));

    if (hum != NULL && s->type == SENSOR_BME280) {
        *hum = (int32_t)((data[6] << 8) | data[7]);
    }

    return ESP_OK;
}

static esp_err_t try_detect(i2c_master_bus_handle_t bus_handle, uint8_t addr, sensor_t *out)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    i2c_master_dev_handle_t dev;
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev);
    if (err != ESP_OK) {
        return err;
    }

    memset(out, 0, sizeof(*out));
    out->dev = dev;
    out->i2c_addr = addr;

    uint8_t chip_id = 0;
    err = reg_read(out, REG_CHIP_ID, &chip_id, 1);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(dev);
        return err;
    }

    if (chip_id == CHIP_ID_BME280) {
        out->type = SENSOR_BME280;
    } else if (chip_id == CHIP_ID_BMP280) {
        out->type = SENSOR_BMP280;
    } else {
        i2c_master_bus_rm_device(dev);
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

esp_err_t bmp280_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "I2C SDA=GPIO%d SCL=GPIO%d", PIN_I2C_SDA, PIN_I2C_SCL);

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    esp_err_t err = ESP_ERR_NOT_FOUND;
    static const uint8_t addrs[] = {ADDR_PRIMARY, ADDR_SECONDARY};
    for (size_t i = 0; i < sizeof(addrs); i++) {
        err = try_detect(bus, addrs[i], &sensor);
        if (err == ESP_OK) {
            break;
        }
    }

    if (err != ESP_OK) {
        return err;
    }

    err = load_calibration(&sensor);
    if (err != ESP_OK) {
        return err;
    }

    initialized = true;
    ESP_LOGI(TAG, "detected %s at 0x%02X",
             sensor.type == SENSOR_BME280 ? "BME280" : "BMP280", sensor.i2c_addr);
    return ESP_OK;
}

esp_err_t bmp280_read(bmp280_reading_t *reading)
{
    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sensor_measure_forced(&sensor);
    if (err != ESP_OK) {
        return err;
    }

    int32_t raw_p = 0, raw_t = 0, raw_h = 0;
    err = read_raw(&sensor, &raw_p, &raw_t, &raw_h);
    if (err != ESP_OK) {
        return err;
    }

    reading->temp_c = compensate_temperature(&sensor, raw_t);
    reading->pressure_hpa = compensate_pressure(&sensor, raw_p);
    reading->has_humidity = sensor.type == SENSOR_BME280;
    if (reading->has_humidity) {
        reading->humidity_pct = compensate_humidity(&sensor, raw_h);
    } else {
        reading->humidity_pct = 0.0f;
    }

    return ESP_OK;
}
