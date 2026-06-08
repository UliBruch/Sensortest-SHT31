/**
 * @file main.c
 * @brief ESP32-C6 Reaktionstest - Einstiegspunkt
 *
 * Demonstriert:
 *  - GPIO-Interrupt für einen Taster (BOOT-Taster, aktiv-low)
 *  - Ansteuerung der adressierbaren Onboard-WS2812-LED
 *  - SSD1306-OLED mit skalierbarer Schrift
 *  - eine kleine FreeRTOS-Task-State-Machine
 *  - präzise Zeitmessung mit esp_timer
 *
 * Funktion: Nach einem Tastendruck vergeht eine zufällige Wartezeit,
 * dann leuchtet die LED. Die Zeit bis zum nächsten Tastendruck wird
 * gemessen und groß auf dem Display ausgegeben.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/i2c_master.h"

#include "app_config.h"
#include "display.h"
#include "reaction_task.h"

static const char *TAG = "MAIN";

static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;

// =============================================================================
// I2C-Bus initialisieren (nur noch für das OLED-Display nötig)
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
    ESP_LOGI(TAG, "ESP32-C6 Reaktionstest");
    ESP_LOGI(TAG, "========================================");

    // I2C-Bus für das Display aufsetzen
    esp_err_t ret = i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C-Init fehlgeschlagen, Abbruch");
        return;
    }

    // Display initialisieren (muss vor dem Reaktionstest-Task geschehen,
    // da der Task direkt auf die Display-Funktionen zugreift)
    ret = display_init(s_i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display-Init fehlgeschlagen, Abbruch");
        return;
    }

    // Reaktionstest starten (richtet Taster und LED ein, startet den Task)
    ret = reaction_task_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reaktionstest konnte nicht gestartet werden");
        return;
    }

    ESP_LOGI(TAG, "Initialisierung abgeschlossen, Reaktionstest läuft");
    // Ab hier übernimmt der Reaktionstest-Task.
}
