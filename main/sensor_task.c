/**
 * @file sensor_task.c
 * @brief Sensor-Task als FreeRTOS-Task
 *
 * Liest im Abstand von SENSOR_INTERVAL_MS den SHT31 aus, gibt Temperatur und
 * relative Feuchte ins Log und auf das OLED-Display aus. Schlägt eine Messung
 * fehl (I2C-Fehler oder CRC), erscheint eine Fehlermeldung auf dem Display und
 * der Task läuft beim nächsten Intervall normal weiter.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "sensor_task.h"
#include "sht31.h"
#include "display.h"
#include "app_config.h"

static const char *TAG = "SENSOR";

static TaskHandle_t s_task_handle = NULL;

// =============================================================================
// Sensor-Task
// =============================================================================
static void sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensor-Task gestartet");

    while (1) {
        float temp_c = 0.0f;
        float hum_pct = 0.0f;

        esp_err_t ret = sht31_read(&temp_c, &hum_pct);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "T = %.2f °C, RH = %.2f rH%%", temp_c, hum_pct);
            display_show_climate(temp_c, hum_pct);
        } else {
            ESP_LOGE(TAG, "Messung fehlgeschlagen: %s", esp_err_to_name(ret));
            display_show_message("SHT31", "FEHLER");
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));
    }
}

// =============================================================================
// Öffentliche API
// =============================================================================
esp_err_t sensor_task_start(void)
{
    BaseType_t ok = xTaskCreate(
        sensor_task,
        "sensor_task",
        TASK_STACK_SENSOR,
        NULL,
        TASK_PRIORITY_SENSOR,
        &s_task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Sensor-Task konnte nicht erstellt werden");
        return ESP_FAIL;
    }

    return ESP_OK;
}
