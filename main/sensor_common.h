/**
 * @file sensor_common.h
 * @brief Shared sensor data structures and synchronization primitives
 */

#ifndef SENSOR_COMMON_H
#define SENSOR_COMMON_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @brief Shared sensor state structure
 *
 * Contains all sensor readings from BME280/BMP280 and SHT31.
 * Access must be protected by g_sensor_mutex.
 */
typedef struct {
    // BME280/BMP280 Data
    float bme_temperature;      ///< Temperature in Celsius
    float bme_humidity;         ///< Humidity in %RH (-1.0 if BMP280, no humidity)
    float bme_pressure;         ///< Pressure in hPa
    bool bme_valid;             ///< True if BME280/BMP280 data is valid
    bool bme_is_bme280;         ///< True if sensor is BME280 (has humidity)

    // SHT31 Data
    float sht_temperature;      ///< Temperature in Celsius
    float sht_humidity;         ///< Humidity in %RH
    bool sht_valid;             ///< True if SHT31 data is valid
} sensor_state_t;

/**
 * @brief Global sensor state
 *
 * Shared between all sensor tasks and consumers.
 * Protected by g_sensor_mutex.
 */
extern sensor_state_t g_sensor_state;

/**
 * @brief Mutex for sensor state access
 *
 * Must be taken before reading or writing g_sensor_state.
 */
extern SemaphoreHandle_t g_sensor_mutex;

/**
 * @brief Binary semaphore: "first sensor data available"
 *
 * Given by each sensor task on every successful read. The MQTT task waits on
 * it once at startup so it doesn't publish empty/uninitialized data before
 * the sensors have run. Demonstrates the classic producer/consumer signaling
 * pattern with a binary semaphore (multiple producers, single consumer).
 */
extern SemaphoreHandle_t g_mqtt_semaphore;

#endif // SENSOR_COMMON_H
