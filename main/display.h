/**
 * @file display.h
 * @brief SSD1306-OLED-Anzeige für den Reaktionstest (passiver Treiber)
 *
 * Anders als im ursprünglichen Sensor-Demo ist dies kein eigener
 * FreeRTOS-Task mehr, sondern eine Sammlung von Zeichenfunktionen.
 * Der Reaktionstest-Task ruft sie auf, um die Zustände darzustellen.
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
 * @brief Zeigt eine Standby-/Aufforderungs-Meldung an (kleine Schrift).
 * @param line1 Obere Zeile (z.B. "Reaktionstest")
 * @param line2 Untere Zeile (z.B. "Taste druecken")
 */
void display_show_message(const char *line1, const char *line2);

/**
 * @brief Zeigt das Ergebnis groß und zentriert an, z.B. "234 ms".
 * @param millis Gemessene Reaktionszeit in Millisekunden
 */
void display_show_result(uint32_t millis);

/**
 * @brief Zeigt eine große, zentrierte Meldung an (z.B. "ZU LANGSAM").
 * @param text Anzuzeigender Text
 */
void display_show_big_text(const char *text);

#endif // DISPLAY_H
