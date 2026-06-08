/**
 * @file bme280_task.c
 * @brief BME280/BMP280 sensor driver and FreeRTOS task implementation
 *
 * Supports both BME280 (temperature, humidity, pressure) and
 * BMP280 (temperature, pressure only) sensors.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "bme280_task.h"
#include "app_config.h"
#include "sensor_common.h"

static const char *TAG = "BME280";

// =============================================================================
// BME280/BMP280 Register Definitions
// =============================================================================
#define BME280_REG_CHIP_ID      0xD0
#define BME280_REG_RESET        0xE0
#define BME280_REG_CTRL_HUM     0xF2
#define BME280_REG_STATUS       0xF3
#define BME280_REG_CTRL_MEAS    0xF4
#define BME280_REG_CONFIG       0xF5
#define BME280_REG_PRESS_MSB    0xF7
#define BME280_REG_CALIB00      0x88
#define BME280_REG_CALIB26      0xE1

#define BME280_RESET_VALUE      0xB6

// Oversampling settings
#define BME280_OSRS_T_X1        (0x01 << 5)
#define BME280_OSRS_P_X1        (0x01 << 2)
#define BME280_OSRS_H_X1        0x01
#define BME280_MODE_FORCED      0x01

// =============================================================================
// Calibration Data Structure
// =============================================================================
typedef struct {
    // Temperature calibration
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;

    // Pressure calibration
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;

    // Humidity calibration (BME280 only)
    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;
} bme280_calib_t;

// =============================================================================
// Module State
// =============================================================================
static i2c_master_dev_handle_t s_dev_handle = NULL;
static bme280_calib_t s_calib;
static bool s_is_bme280 = false;
static int32_t s_t_fine = 0;

// =============================================================================
// I2C Helper Functions
// =============================================================================
static esp_err_t bme280_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev_handle, &reg, 1, data, len,
                                        APP_I2C_TIMEOUT_MS);
}

static esp_err_t bme280_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev_handle, buf, 2, APP_I2C_TIMEOUT_MS);
}

// =============================================================================
// Sensor Initialization
// =============================================================================
static esp_err_t bme280_read_calibration(void)
{
    uint8_t calib_data[26];
    esp_err_t ret;

    // Read calibration data from 0x88-0xA1 (26 bytes)
    ret = bme280_read_reg(BME280_REG_CALIB00, calib_data, 26);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse temperature calibration
    s_calib.dig_T1 = (uint16_t)(calib_data[1] << 8 | calib_data[0]);
    s_calib.dig_T2 = (int16_t)(calib_data[3] << 8 | calib_data[2]);
    s_calib.dig_T3 = (int16_t)(calib_data[5] << 8 | calib_data[4]);

    // Parse pressure calibration
    s_calib.dig_P1 = (uint16_t)(calib_data[7] << 8 | calib_data[6]);
    s_calib.dig_P2 = (int16_t)(calib_data[9] << 8 | calib_data[8]);
    s_calib.dig_P3 = (int16_t)(calib_data[11] << 8 | calib_data[10]);
    s_calib.dig_P4 = (int16_t)(calib_data[13] << 8 | calib_data[12]);
    s_calib.dig_P5 = (int16_t)(calib_data[15] << 8 | calib_data[14]);
    s_calib.dig_P6 = (int16_t)(calib_data[17] << 8 | calib_data[16]);
    s_calib.dig_P7 = (int16_t)(calib_data[19] << 8 | calib_data[18]);
    s_calib.dig_P8 = (int16_t)(calib_data[21] << 8 | calib_data[20]);
    s_calib.dig_P9 = (int16_t)(calib_data[23] << 8 | calib_data[22]);

    // Humidity calibration H1 is at 0xA1
    s_calib.dig_H1 = calib_data[25];

    // Read humidity calibration from 0xE1-0xE7 (BME280 only)
    if (s_is_bme280) {
        uint8_t hum_calib[7];
        ret = bme280_read_reg(BME280_REG_CALIB26, hum_calib, 7);
        if (ret != ESP_OK) {
            return ret;
        }

        s_calib.dig_H2 = (int16_t)(hum_calib[1] << 8 | hum_calib[0]);
        s_calib.dig_H3 = hum_calib[2];
        s_calib.dig_H4 = (int16_t)((hum_calib[3] << 4) | (hum_calib[4] & 0x0F));
        s_calib.dig_H5 = (int16_t)((hum_calib[5] << 4) | (hum_calib[4] >> 4));
        s_calib.dig_H6 = (int8_t)hum_calib[6];
    }

    return ESP_OK;
}

static esp_err_t bme280_init(i2c_master_bus_handle_t bus_handle)
{
    esp_err_t ret;

    // Add device to I2C bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BME280_I2C_ADDR,
        .scl_speed_hz = APP_I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Read chip ID
    uint8_t chip_id;
    ret = bme280_read_reg(BME280_REG_CHIP_ID, &chip_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID: %s", esp_err_to_name(ret));
        return ret;
    }

    if (chip_id == BME280_CHIP_ID) {
        s_is_bme280 = true;
        ESP_LOGI(TAG, "BME280 detected (ID: 0x%02X) - Temperature, Humidity, Pressure", chip_id);
    } else if (chip_id == BMP280_CHIP_ID) {
        s_is_bme280 = false;
        ESP_LOGI(TAG, "BMP280 detected (ID: 0x%02X) - Temperature, Pressure only", chip_id);
    } else {
        ESP_LOGE(TAG, "Unknown chip ID: 0x%02X", chip_id);
        return ESP_ERR_NOT_FOUND;
    }

    // Soft reset
    ret = bme280_write_reg(BME280_REG_RESET, BME280_RESET_VALUE);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    // Read calibration data
    ret = bme280_read_calibration();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure sensor for forced mode
    if (s_is_bme280) {
        ret = bme280_write_reg(BME280_REG_CTRL_HUM, BME280_OSRS_H_X1);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

// =============================================================================
// Compensation Functions (from Bosch datasheet)
// =============================================================================
static float bme280_compensate_temperature(int32_t adc_T)
{
    int32_t var1, var2;

    var1 = ((((adc_T >> 3) - ((int32_t)s_calib.dig_T1 << 1))) *
            ((int32_t)s_calib.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)s_calib.dig_T1)) *
              ((adc_T >> 4) - ((int32_t)s_calib.dig_T1))) >> 12) *
            ((int32_t)s_calib.dig_T3)) >> 14;

    s_t_fine = var1 + var2;

    return (float)((s_t_fine * 5 + 128) >> 8) / 100.0f;
}

static float bme280_compensate_pressure(int32_t adc_P)
{
    int64_t var1, var2, p;

    var1 = ((int64_t)s_t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)s_calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)s_calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)s_calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)s_calib.dig_P3) >> 8) +
           ((var1 * (int64_t)s_calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)s_calib.dig_P1) >> 33;

    if (var1 == 0) {
        return 0;
    }

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)s_calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)s_calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)s_calib.dig_P7) << 4);

    return (float)((uint32_t)p) / 256.0f / 100.0f;  // Return in hPa
}

static float bme280_compensate_humidity(int32_t adc_H)
{
    int32_t v_x1_u32r;

    v_x1_u32r = (s_t_fine - ((int32_t)76800));
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)s_calib.dig_H4) << 20) -
                   (((int32_t)s_calib.dig_H5) * v_x1_u32r)) +
                  ((int32_t)16384)) >> 15) *
                (((((((v_x1_u32r * ((int32_t)s_calib.dig_H6)) >> 10) *
                     (((v_x1_u32r * ((int32_t)s_calib.dig_H3)) >> 11) +
                      ((int32_t)32768))) >> 10) +
                   ((int32_t)2097152)) *
                  ((int32_t)s_calib.dig_H2) + 8192) >> 14));

    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                               ((int32_t)s_calib.dig_H1)) >> 4));

    v_x1_u32r = (v_x1_u32r < 0) ? 0 : v_x1_u32r;
    v_x1_u32r = (v_x1_u32r > 419430400) ? 419430400 : v_x1_u32r;

    return (float)((uint32_t)(v_x1_u32r >> 12)) / 1024.0f;
}

// =============================================================================
// Measurement
// =============================================================================
static esp_err_t bme280_read_measurement(float *temperature, float *pressure, float *humidity)
{
    esp_err_t ret;

    // Start forced mode measurement
    uint8_t ctrl_meas = BME280_OSRS_T_X1 | BME280_OSRS_P_X1 | BME280_MODE_FORCED;
    ret = bme280_write_reg(BME280_REG_CTRL_MEAS, ctrl_meas);
    if (ret != ESP_OK) {
        return ret;
    }

    // Wait for measurement to complete
    vTaskDelay(pdMS_TO_TICKS(50));

    // Read raw data (pressure: 3 bytes, temperature: 3 bytes, humidity: 2 bytes)
    uint8_t data[8];
    ret = bme280_read_reg(BME280_REG_PRESS_MSB, data, 8);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse raw values
    int32_t adc_P = (int32_t)((data[0] << 12) | (data[1] << 4) | (data[2] >> 4));
    int32_t adc_T = (int32_t)((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));
    int32_t adc_H = (int32_t)((data[6] << 8) | data[7]);

    // Compensate values (temperature must be first as it sets t_fine)
    *temperature = bme280_compensate_temperature(adc_T);
    *pressure = bme280_compensate_pressure(adc_P);

    if (s_is_bme280) {
        *humidity = bme280_compensate_humidity(adc_H);
    } else {
        *humidity = SENSOR_HUMIDITY_INVALID;
    }

    return ESP_OK;
}

// =============================================================================
// FreeRTOS Task
// =============================================================================
static void bme280_task(void *pvParameters)
{
    float temperature, pressure, humidity;

    ESP_LOGI(TAG, "BME280 Task started");

    while (1) {
        esp_err_t ret = bme280_read_measurement(&temperature, &pressure, &humidity);

        if (ret == ESP_OK) {
            // Update shared state with mutex protection
            xSemaphoreTake(g_sensor_mutex, portMAX_DELAY);
            g_sensor_state.bme_temperature = temperature;
            g_sensor_state.bme_pressure = pressure;
            g_sensor_state.bme_humidity = humidity;
            g_sensor_state.bme_valid = true;
            g_sensor_state.bme_is_bme280 = s_is_bme280;
            xSemaphoreGive(g_sensor_mutex);

            // Signal "fresh data available" to the MQTT task. With a binary
            // semaphore, repeated gives saturate harmlessly at 1, so multiple
            // sensor tasks giving in parallel cause no accumulation.
            xSemaphoreGive(g_mqtt_semaphore);

            if (s_is_bme280) {
                ESP_LOGI(TAG, "T=%.2fC, P=%.2fhPa, H=%.2f%%",
                         temperature, pressure, humidity);
            } else {
                ESP_LOGI(TAG, "T=%.2fC, P=%.2fhPa (no humidity)",
                         temperature, pressure);
            }
        } else {
            ESP_LOGW(TAG, "Measurement failed: %s", esp_err_to_name(ret));

            xSemaphoreTake(g_sensor_mutex, portMAX_DELAY);
            g_sensor_state.bme_valid = false;
            xSemaphoreGive(g_sensor_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}

// =============================================================================
// Public API
// =============================================================================
esp_err_t bme280_task_start(i2c_master_bus_handle_t i2c_bus_handle)
{
    esp_err_t ret = bme280_init(i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BME280 initialization failed");
        return ret;
    }

    BaseType_t xReturned = xTaskCreate(
        bme280_task,
        "bme280_task",
        TASK_STACK_BME280,
        NULL,
        TASK_PRIORITY_BME280,
        NULL
    );

    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
