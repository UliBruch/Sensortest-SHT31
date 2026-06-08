/**
 * @file display_task.h
 * @brief SSD1306 OLED display task interface
 */

#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

#include "driver/i2c_master.h"

/**
 * @brief Start the display task
 *
 * Initializes the SSD1306 OLED display and creates a FreeRTOS task
 * that periodically updates the display with sensor readings.
 *
 * @param i2c_bus_handle Handle to the initialized I2C master bus
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t display_task_start(i2c_master_bus_handle_t i2c_bus_handle);

#endif // DISPLAY_TASK_H
