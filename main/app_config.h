/**
 * @file app_config.h
 * @brief Hardware-Konfiguration und Konstanten für den ESP32-C6 Reaktionstest
 *
 * Board: ESP32-C6-DevKitC-1
 *  - BOOT-Taster      : GPIO 9  (aktiv-low, interner Pull-up)
 *  - Onboard WS2812 LED: GPIO 8 (adressierbare RGB-LED)
 *  - SSD1306 OLED      : I2C, SDA = GPIO 5, SCL = GPIO 6, 128x32, Addr 0x3C
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"

// =============================================================================
// I2C-Bus (OLED-Display)
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
// Taster und LED
// =============================================================================
#define BUTTON_GPIO             GPIO_NUM_9   // BOOT-Taster, aktiv-low
#define BUTTON_DEBOUNCE_MS      50           // Mindestabstand zwischen zwei Events

#define LED_GPIO                GPIO_NUM_8   // Onboard WS2812 RGB-LED
#define LED_BRIGHTNESS          40           // 0..255 pro Farbkanal (augenschonend)

// =============================================================================
// Reaktionstest-Timing
// =============================================================================
#define WAIT_MIN_MS             2000   // kürzeste Zufallswartezeit
#define WAIT_MAX_MS             5000   // längste Zufallswartezeit
#define REACTION_TIMEOUT_MS     2000   // Timeout, bis das System in Standby geht
#define RESULT_DISPLAY_MS       3000   // Anzeigedauer des Ergebnisses
#define TIMEOUT_DISPLAY_MS      2500   // Anzeigedauer der Timeout-Meldung

// =============================================================================
// Task-Konfiguration
// =============================================================================
#define TASK_PRIORITY_REACTION  5
#define TASK_STACK_REACTION     4096

#endif // APP_CONFIG_H
