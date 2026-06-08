/**
 * @file sht31.h
 * @brief Treiber für den Sensirion SHT31 Temperatur-/Feuchtesensor (I2C)
 *
 * Schlanker Treiber für den SHT31 über die ESP-IDF-I2C-Master-API. Der
 * Sensor wird im Single-Shot-Modus (High Repeatability, ohne Clock-Stretching)
 * betrieben: Pro Messung wird ein Befehl gesendet, kurz gewartet und das
 * 6-Byte-Ergebnis inklusive CRC gelesen.
 */

#ifndef SHT31_H
#define SHT31_H

#include "driver/i2c_master.h"

/**
 * @brief Fügt den SHT31 als Gerät an den bereits initialisierten I2C-Bus.
 * @param bus_handle Handle des I2C-Master-Bus
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t sht31_init(i2c_master_bus_handle_t bus_handle);

/**
 * @brief Führt eine Single-Shot-Messung durch und liefert die Werte zurück.
 * @param[out] temp_c   Temperatur in Grad Celsius
 * @param[out] hum_pct  Relative Feuchte in Prozent (0..100)
 * @return ESP_OK bei Erfolg, ESP_ERR_INVALID_CRC bei CRC-Fehler,
 *         sonst der I2C-Fehlercode
 */
esp_err_t sht31_read(float *temp_c, float *hum_pct);

#endif // SHT31_H
