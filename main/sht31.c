/**
 * @file sht31.c
 * @brief Treiber für den Sensirion SHT31 Temperatur-/Feuchtesensor
 *
 * Ablauf einer Messung (Single-Shot, High Repeatability, ohne Clock-Stretching):
 *   1. 2-Byte-Messbefehl senden (0x24 0x00).
 *   2. Messzeit abwarten (max. 15 ms laut Datenblatt, hier 20 ms mit Reserve).
 *   3. 6 Bytes lesen: [T_MSB, T_LSB, T_CRC, RH_MSB, RH_LSB, RH_CRC].
 *   4. Beide CRC-8-Prüfsummen kontrollieren und die Rohwerte umrechnen.
 *
 * Umrechnung laut Datenblatt:
 *   T[°C] = -45 + 175 * S_T / 65535
 *   RH[%] = 100 * S_RH / 65535
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "sht31.h"
#include "app_config.h"

static const char *TAG = "SHT31";

// =============================================================================
// Modul-Zustand
// =============================================================================
static i2c_master_dev_handle_t s_dev_handle = NULL;

// =============================================================================
// CRC-8 nach Sensirion: Polynom 0x31, Init 0xFF, kein Reflect, kein Final-XOR.
// =============================================================================
static uint8_t sht31_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

// =============================================================================
// Öffentliche API
// =============================================================================
esp_err_t sht31_init(i2c_master_bus_handle_t bus_handle)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT31_I2C_ADDR,
        .scl_speed_hz = APP_I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gerät konnte nicht zum I2C-Bus hinzugefügt werden: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    // Soft-Reset, damit der Sensor in einem definierten Zustand startet.
    const uint8_t reset_cmd[2] = {SHT31_CMD_RESET_MSB, SHT31_CMD_RESET_LSB};
    ret = i2c_master_transmit(s_dev_handle, reset_cmd, sizeof(reset_cmd),
                              APP_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Soft-Reset fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    // Nach dem Reset braucht der Sensor bis zu 1,5 ms; großzügig warten.
    vTaskDelay(pdMS_TO_TICKS(5));

    ESP_LOGI(TAG, "SHT31 an Adresse 0x%02X initialisiert", SHT31_I2C_ADDR);
    return ESP_OK;
}

esp_err_t sht31_read(float *temp_c, float *hum_pct)
{
    // 1. Messbefehl senden.
    const uint8_t meas_cmd[2] = {SHT31_CMD_MEAS_MSB, SHT31_CMD_MEAS_LSB};
    esp_err_t ret = i2c_master_transmit(s_dev_handle, meas_cmd, sizeof(meas_cmd),
                                        APP_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Messbefehl fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Messzeit abwarten.
    vTaskDelay(pdMS_TO_TICKS(SHT31_MEAS_DELAY_MS));

    // 3. Ergebnis lesen.
    uint8_t data[6];
    ret = i2c_master_receive(s_dev_handle, data, sizeof(data), APP_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Lesen fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. CRC beider 16-Bit-Worte prüfen.
    if (sht31_crc8(&data[0], 2) != data[2] ||
        sht31_crc8(&data[3], 2) != data[5]) {
        ESP_LOGW(TAG, "CRC-Fehler bei der Sensorantwort");
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_t = ((uint16_t)data[0] << 8) | data[1];
    uint16_t raw_rh = ((uint16_t)data[3] << 8) | data[4];

    float t = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
    float rh = 100.0f * ((float)raw_rh / 65535.0f);

    // Feuchte auf den gültigen Bereich begrenzen.
    if (rh < 0.0f) {
        rh = 0.0f;
    } else if (rh > 100.0f) {
        rh = 100.0f;
    }

    if (temp_c) {
        *temp_c = t;
    }
    if (hum_pct) {
        *hum_pct = rh;
    }
    return ESP_OK;
}
