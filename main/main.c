/**
 * @file main.c
 * @brief ESP32-C6 SHT31-Sensortest - Einstiegspunkt
 *
 * Demonstriert:
 *  - I2C-Master mit zwei Geräten am selben Bus (Sensor + Display)
 *  - Ansteuerung des SHT31-Temperatur-/Feuchtesensors inkl. CRC-Prüfung
 *  - SSD1306-OLED mit skalierbarer Schrift
 *  - einen einfachen FreeRTOS-Task zur zyklischen Messung
 *
 * Funktion: Der SHT31 wird zyklisch ausgelesen; Temperatur und relative
 * Feuchte werden ins Log und auf das OLED-Display ausgegeben.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/i2c_master.h"

#include "app_config.h"
#include "display.h"
#include "sht31.h"
#include "sensor_task.h"

static const char *TAG = "MAIN";

static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;

// =============================================================================
// I2C-Bus initialisieren (gemeinsam für SHT31-Sensor und OLED-Display)
// =============================================================================
static esp_err_t i2c_bus_init(void)
{
    ESP_LOGI(TAG, "Initialisiere I2C-Bus (SCL=%d, SDA=%d, %d Hz)",
             APP_I2C_SCL_PIN, APP_I2C_SDA_PIN, APP_I2C_FREQ_HZ);

    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = APP_I2C_PORT,
        .scl_io_num = APP_I2C_SCL_PIN,
        .sda_io_num = APP_I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C-Bus konnte nicht erstellt werden: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C-Bus initialisiert");
    return ESP_OK;
}

// =============================================================================
// Einstiegspunkt
// =============================================================================
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32-C6 SHT31-Sensortest");
    ESP_LOGI(TAG, "========================================");

    // I2C-Bus für Sensor und Display aufsetzen
    esp_err_t ret = i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C-Init fehlgeschlagen, Abbruch");
        return;
    }

    // Display initialisieren (muss vor dem Sensor-Task geschehen, da der
    // Task direkt auf die Display-Funktionen zugreift)
    ret = display_init(s_i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display-Init fehlgeschlagen, Abbruch");
        return;
    }
    display_show_message("SHT31", "START");

    // SHT31-Sensor am selben I2C-Bus initialisieren
    ret = sht31_init(s_i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHT31-Init fehlgeschlagen, Abbruch");
        display_show_message("SHT31", "NICHT DA");
        return;
    }

    // Sensor-Task starten (liest zyklisch und aktualisiert das Display)
    ret = sensor_task_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Sensor-Task konnte nicht gestartet werden");
        return;
    }

    ESP_LOGI(TAG, "Initialisierung abgeschlossen, Sensor-Task läuft");
    // Ab hier übernimmt der Sensor-Task.
}
