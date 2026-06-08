/**
 * @file display.h
 * @brief SSD1306-OLED-Anzeige für den SHT31-Sensortest (passiver Treiber)
 *
 * Dies ist kein eigener FreeRTOS-Task, sondern eine Sammlung von
 * Zeichenfunktionen. Der Sensor-Task ruft sie auf, um Meldungen und die
 * Messwerte darzustellen.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "driver/i2c_master.h"

/**
 * @brief Initialisiert das SSD1306-Display am angegebenen I2C-Bus.
 * @param i2c_bus_handle Handle des bereits initialisierten I2C-Master-Bus
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t display_init(i2c_master_bus_handle_t i2c_bus_handle);

/**
 * @brief Zeigt eine zweizeilige Meldung an (z.B. Start- oder Fehlertext).
 * @param line1 Obere Zeile, kleine Schrift (z.B. "SHT31")
 * @param line2 Untere Zeile, große Schrift (z.B. "FEHLER")
 */
void display_show_message(const char *line1, const char *line2);

/**
 * @brief Zeigt Temperatur und relative Feuchte in zwei Zeilen an.
 * @param temp_c  Temperatur in Grad Celsius
 * @param hum_pct Relative Feuchte in Prozent
 */
void display_show_climate(float temp_c, float hum_pct);

#endif // DISPLAY_H
