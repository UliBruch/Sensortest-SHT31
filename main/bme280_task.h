/**
 * @file bme280_task.h
 * @brief BME280/BMP280 sensor task interface
 */

#ifndef BME280_TASK_H
#define BME280_TASK_H

#include "driver/i2c_master.h"

/**
 * @brief Start the BME280/BMP280 sensor task
 *
 * Initializes the BME280 or BMP280 sensor and creates a FreeRTOS task
 * that periodically reads temperature, pressure, and humidity (BME280 only).
 *
 * @param i2c_bus_handle Handle to the initialized I2C master bus
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bme280_task_start(i2c_master_bus_handle_t i2c_bus_handle);

#endif // BME280_TASK_H
