/**
 * @file sht31_task.c
 * @brief SHT31 temperature/humidity sensor driver and FreeRTOS task implementation
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "sht31_task.h"
#include "app_config.h"
#include "sensor_common.h"

static const char *TAG = "SHT31";

// =============================================================================
// SHT31 Command Definitions
// =============================================================================
// Single-shot measurement commands (Clock stretching disabled)
#define SHT31_CMD_MEAS_HIGH_REP_MSB     0x24
#define SHT31_CMD_MEAS_HIGH_REP_LSB     0x00
#define SHT31_CMD_MEAS_MED_REP_MSB      0x24
#define SHT31_CMD_MEAS_MED_REP_LSB      0x0B
#define SHT31_CMD_MEAS_LOW_REP_MSB      0x24
#define SHT31_CMD_MEAS_LOW_REP_LSB      0x16

// Soft reset command
#define SHT31_CMD_SOFT_RESET_MSB        0x30
#define SHT31_CMD_SOFT_RESET_LSB        0xA2

// Status register
#define SHT31_CMD_READ_STATUS_MSB       0xF3
#define SHT31_CMD_READ_STATUS_LSB       0x2D

// =============================================================================
// Module State
// =============================================================================
static i2c_master_dev_handle_t s_dev_handle = NULL;

// =============================================================================
// CRC Calculation (Polynomial: 0x31, Init: 0xFF)
// =============================================================================
static uint8_t sht31_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

// =============================================================================
// I2C Helper Functions
// =============================================================================
static esp_err_t sht31_send_command(uint8_t msb, uint8_t lsb)
{
    uint8_t cmd[2] = {msb, lsb};
    return i2c_master_transmit(s_dev_handle, cmd, 2, APP_I2C_TIMEOUT_MS);
}

// =============================================================================
// Sensor Initialization
// =============================================================================
static esp_err_t sht31_init(i2c_master_bus_handle_t bus_handle)
{
    esp_err_t ret;

    // Add device to I2C bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT31_I2C_ADDR,
        .scl_speed_hz = APP_I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Soft reset
    ret = sht31_send_command(SHT31_CMD_SOFT_RESET_MSB, SHT31_CMD_SOFT_RESET_LSB);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Soft reset failed: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    // Read status register to verify sensor is responding
    uint8_t status_cmd[2] = {SHT31_CMD_READ_STATUS_MSB, SHT31_CMD_READ_STATUS_LSB};
    uint8_t status_data[3];

    ret = i2c_master_transmit_receive(s_dev_handle, status_cmd, 2,
                                       status_data, 3, APP_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read status: %s", esp_err_to_name(ret));
        return ret;
    }

    // Verify CRC
    uint8_t crc = sht31_crc8(status_data, 2);
    if (crc != status_data[2]) {
        ESP_LOGE(TAG, "Status CRC mismatch: expected 0x%02X, got 0x%02X",
                 crc, status_data[2]);
        return ESP_ERR_INVALID_CRC;
    }

    ESP_LOGI(TAG, "SHT31 initialized, status: 0x%02X%02X",
             status_data[0], status_data[1]);

    return ESP_OK;
}

// =============================================================================
// Measurement
// =============================================================================
static esp_err_t sht31_read_measurement(float *temperature, float *humidity)
{
    esp_err_t ret;

    // Start single-shot measurement with high repeatability
    ret = sht31_send_command(SHT31_CMD_MEAS_HIGH_REP_MSB, SHT31_CMD_MEAS_HIGH_REP_LSB);
    if (ret != ESP_OK) {
        return ret;
    }

    // Wait for measurement to complete (high repeatability takes ~15ms)
    vTaskDelay(pdMS_TO_TICKS(20));

    // Read measurement data (6 bytes: temp_msb, temp_lsb, temp_crc, hum_msb, hum_lsb, hum_crc)
    uint8_t data[6];
    ret = i2c_master_receive(s_dev_handle, data, 6, APP_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    // Verify temperature CRC
    uint8_t temp_crc = sht31_crc8(&data[0], 2);
    if (temp_crc != data[2]) {
        ESP_LOGW(TAG, "Temperature CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    // Verify humidity CRC
    uint8_t hum_crc = sht31_crc8(&data[3], 2);
    if (hum_crc != data[5]) {
        ESP_LOGW(TAG, "Humidity CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    // Convert raw values to temperature and humidity
    uint16_t raw_temp = (data[0] << 8) | data[1];
    uint16_t raw_hum = (data[3] << 8) | data[4];

    // Temperature: -45 + 175 * (raw / 65535)
    *temperature = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);

    // Humidity: 100 * (raw / 65535)
    *humidity = 100.0f * ((float)raw_hum / 65535.0f);

    return ESP_OK;
}

// =============================================================================
// FreeRTOS Task
// =============================================================================
static void sht31_task(void *pvParameters)
{
    float temperature, humidity;

    ESP_LOGI(TAG, "SHT31 Task started");

    while (1) {
        esp_err_t ret = sht31_read_measurement(&temperature, &humidity);

        if (ret == ESP_OK) {
            // Update shared state with mutex protection
            xSemaphoreTake(g_sensor_mutex, portMAX_DELAY);
            g_sensor_state.sht_temperature = temperature;
            g_sensor_state.sht_humidity = humidity;
            g_sensor_state.sht_valid = true;
            xSemaphoreGive(g_sensor_mutex);

            // Signal "fresh data available" to the MQTT task (see bme280_task.c)
            xSemaphoreGive(g_mqtt_semaphore);

            ESP_LOGI(TAG, "T=%.2fC, H=%.2f%%", temperature, humidity);
        } else {
            ESP_LOGW(TAG, "Measurement failed: %s", esp_err_to_name(ret));

            xSemaphoreTake(g_sensor_mutex, portMAX_DELAY);
            g_sensor_state.sht_valid = false;
            xSemaphoreGive(g_sensor_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}

// =============================================================================
// Public API
// =============================================================================
esp_err_t sht31_task_start(i2c_master_bus_handle_t i2c_bus_handle)
{
    esp_err_t ret = sht31_init(i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHT31 initialization failed");
        return ret;
    }

    BaseType_t xReturned = xTaskCreate(
        sht31_task,
        "sht31_task",
        TASK_STACK_SHT31,
        NULL,
        TASK_PRIORITY_SHT31,
        NULL
    );

    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
