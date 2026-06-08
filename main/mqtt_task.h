/**
 * @file mqtt_task.h
 * @brief MQTT task interface (stub for future implementation)
 */

#ifndef MQTT_TASK_H
#define MQTT_TASK_H

#include "esp_err.h"

/**
 * @brief Start the MQTT task
 *
 * Creates a FreeRTOS task that waits for sensor data updates
 * and logs them (future: publishes to MQTT broker).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_task_start(void);

#endif // MQTT_TASK_H
