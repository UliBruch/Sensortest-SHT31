/**
 * @file app_config.h
 * @brief Hardware-Konfiguration und Konstanten für den ESP32-C6 SHT31-Sensortest
 *
 * Board: ESP32-C6-DevKitC-1
 *  - SHT31 Temp/Feuchte: I2C, SDA = GPIO 5, SCL = GPIO 6, Addr 0x44
 *  - SSD1306 OLED       : I2C, SDA = GPIO 5, SCL = GPIO 6, 128x32, Addr 0x3C
 *
 * SHT31 und Display teilen sich denselben I2C-Bus.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"

// =============================================================================
// I2C-Bus (gemeinsam für OLED-Display und SHT31-Sensor)
// =============================================================================
#define APP_I2C_PORT            I2C_NUM_0
#define APP_I2C_SCL_PIN         GPIO_NUM_6
#define APP_I2C_SDA_PIN         GPIO_NUM_5
#define APP_I2C_FREQ_HZ         100000      // 100 kHz (Standard Mode)
#define APP_I2C_TIMEOUT_MS      1000

#define SSD1306_I2C_ADDR        0x3C

// =============================================================================
// Display-Geometrie
// =============================================================================
#define DISPLAY_WIDTH           128
#define DISPLAY_HEIGHT          32
#define DISPLAY_PAGES           (DISPLAY_HEIGHT / 8)  // 4 Pages bei 32px Höhe

// =============================================================================
// SHT31-Sensor
// =============================================================================
// Standard-Adresse 0x44 (ADDR-Pin auf GND). Bei ADDR-Pin auf VDD: 0x45.
#define SHT31_I2C_ADDR          0x44
// Single-Shot, High Repeatability, ohne Clock-Stretching (MSB/LSB).
#define SHT31_CMD_MEAS_MSB      0x24
#define SHT31_CMD_MEAS_LSB      0x00
// Soft-Reset-Kommando.
#define SHT31_CMD_RESET_MSB     0x30
#define SHT31_CMD_RESET_LSB     0xA2
// Messdauer bei High Repeatability laut Datenblatt max. 15 ms; mit Reserve.
#define SHT31_MEAS_DELAY_MS     20

// =============================================================================
// Sensor-Task-Timing
// =============================================================================
#define SENSOR_INTERVAL_MS      1000   // Abstand zwischen zwei Messungen

// =============================================================================
// Task-Konfiguration
// =============================================================================
#define TASK_PRIORITY_SENSOR    5
#define TASK_STACK_SENSOR       4096

#endif // APP_CONFIG_H
