/**
 * @file sht31_task.h
 * @brief SHT31 temperature/humidity sensor task interface
 */

#ifndef SHT31_TASK_H
#define SHT31_TASK_H

#include "driver/i2c_master.h"

/**
 * @brief Start the SHT31 sensor task
 *
 * Initializes the SHT31 sensor and creates a FreeRTOS task
 * that periodically reads temperature and humidity.
 *
 * @param i2c_bus_handle Handle to the initialized I2C master bus
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sht31_task_start(i2c_master_bus_handle_t i2c_bus_handle);

#endif // SHT31_TASK_H
