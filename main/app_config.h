/**
 * @file app_config.h
 * @brief Hardware configuration and constants for ESP32-C6 Sensor Project
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"

// =============================================================================
// I2C Bus Configuration
// =============================================================================
#define APP_I2C_PORT            I2C_NUM_0
#define APP_I2C_SCL_PIN         GPIO_NUM_6
#define APP_I2C_SDA_PIN         GPIO_NUM_5
#define APP_I2C_FREQ_HZ         100000      // 100 kHz (Standard Mode)
#define APP_I2C_TIMEOUT_MS      1000

// =============================================================================
// Sensor I2C Addresses
// =============================================================================
#define BME280_I2C_ADDR         0x76
#define SHT31_I2C_ADDR          0x44
#define SSD1306_I2C_ADDR        0x3C

// =============================================================================
// BME280/BMP280 Chip IDs
// =============================================================================
#define BME280_CHIP_ID          0x60
#define BMP280_CHIP_ID          0x58

// =============================================================================
// Display Configuration
// =============================================================================
#define DISPLAY_WIDTH           128
#define DISPLAY_HEIGHT          32
#define DISPLAY_PAGES           (DISPLAY_HEIGHT / 8)  // 4 pages for 32px height

// =============================================================================
// Task Configuration
// =============================================================================
// Task Priorities (higher number = higher priority)
#define TASK_PRIORITY_BME280    3
#define TASK_PRIORITY_SHT31     3
#define TASK_PRIORITY_DISPLAY   2
#define TASK_PRIORITY_MQTT      4

// Task Stack Sizes (in bytes)
#define TASK_STACK_BME280       4096
#define TASK_STACK_SHT31        4096
#define TASK_STACK_DISPLAY      4096
#define TASK_STACK_MQTT         6144   // larger because cloud client uses TLS

// Task Intervals (in milliseconds)
#define SENSOR_READ_INTERVAL_MS    5000
#define DISPLAY_UPDATE_INTERVAL_MS 5000
#define MQTT_PUBLISH_INTERVAL_MS   5000

// =============================================================================
// Sensor Invalid Values
// =============================================================================
#define SENSOR_HUMIDITY_INVALID -1.0f

#endif // APP_CONFIG_H
