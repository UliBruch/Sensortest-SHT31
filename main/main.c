/**
 * @file main.c
 * @brief ESP32-C6 Sensor-Display Project - Application Entry Point
 *
 * This application demonstrates:
 * - FreeRTOS multi-task architecture
 * - I2C sensor communication (BME280/BMP280, SHT31)
 * - SSD1306 OLED display output
 * - Mutex-protected shared state
 * - Semaphore-based task signaling (for future MQTT)
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"

#include "app_config.h"
#include "sensor_common.h"
#include "bme280_task.h"
#include "sht31_task.h"
#include "display_task.h"
#include "wifi_manager.h"
#include "time_sync.h"
#include "mqtt_task.h"

static const char *TAG = "MAIN";

// =============================================================================
// Global State (defined in sensor_common.h as extern)
// =============================================================================
sensor_state_t g_sensor_state = {0};
SemaphoreHandle_t g_sensor_mutex = NULL;
SemaphoreHandle_t g_mqtt_semaphore = NULL;

// =============================================================================
// I2C Bus Handle
// =============================================================================
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;

// =============================================================================
// Reset Reason
// =============================================================================
static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON:    return "POWERON (cold boot)";
        case ESP_RST_EXT:        return "EXT (external pin)";
        case ESP_RST_SW:         return "SW (esp_restart)";
        case ESP_RST_PANIC:      return "PANIC (exception/abort)";
        case ESP_RST_INT_WDT:    return "INT_WDT (interrupt watchdog)";
        case ESP_RST_TASK_WDT:   return "TASK_WDT (task watchdog)";
        case ESP_RST_WDT:        return "WDT (other watchdog)";
        case ESP_RST_DEEPSLEEP:  return "DEEPSLEEP (wake from deep sleep)";
        case ESP_RST_BROWNOUT:   return "BROWNOUT (low voltage)";
        case ESP_RST_SDIO:       return "SDIO";
        case ESP_RST_USB:        return "USB";
        case ESP_RST_JTAG:       return "JTAG";
        case ESP_RST_UNKNOWN:    return "UNKNOWN";
        default:                 return "?";
    }
}

// =============================================================================
// I2C Bus Initialization
//
// Bus synchronization note: The IDF v5.x i2c_master driver is thread-safe per
// bus - it serializes transactions internally with its own mutex. As long as
// each device handle (BME280, SHT31, SSD1306) is used by exactly one task,
// no application-level bus lock is needed. The g_sensor_mutex in this file
// protects the shared sensor_state_t, NOT the I2C bus.
// =============================================================================
static esp_err_t i2c_bus_init(void)
{
    ESP_LOGI(TAG, "Initializing I2C bus (SCL=%d, SDA=%d, %d Hz)",
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
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus initialized");
    return ESP_OK;
}

// =============================================================================
// Application Entry Point
// =============================================================================
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32-C6 Sensor-Display Project");
    ESP_LOGI(TAG, "========================================");

    esp_reset_reason_t reset_reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %s", reset_reason_str(reset_reason));

    esp_err_t ret;

    // Initialize synchronization primitives
    ESP_LOGI(TAG, "Creating synchronization primitives...");

    g_sensor_mutex = xSemaphoreCreateMutex();
    if (g_sensor_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor mutex");
        return;
    }

    g_mqtt_semaphore = xSemaphoreCreateBinary();
    if (g_mqtt_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT semaphore");
        return;
    }

    ESP_LOGI(TAG, "Mutex and semaphore created");

    // Initialize shared state
    memset(&g_sensor_state, 0, sizeof(g_sensor_state));
    g_sensor_state.bme_humidity = SENSOR_HUMIDITY_INVALID;  // Mark as invalid initially

    // Initialize NVS (required by WiFi for storing AP info)
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    // Connect to WiFi (blocks until connected or fails)
    bool wifi_up = (wifi_manager_start() == ESP_OK);
    if (!wifi_up) {
        ESP_LOGW(TAG, "WiFi connection failed - continuing without network");
    } else {
        // Sync wall clock from NTP so our MQTT timestamps are real time
        time_sync_start(10000);
    }

    // Initialize I2C bus
    ret = i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus initialization failed, stopping");
        return;
    }

    // Start sensor and display tasks
    ESP_LOGI(TAG, "Starting tasks...");

    // Start BME280 task
    ret = bme280_task_start(s_i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BME280 task start failed (sensor may not be connected)");
    }

    // Start SHT31 task
    ret = sht31_task_start(s_i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SHT31 task start failed (sensor may not be connected)");
    }

    // Start display task
    ret = display_task_start(s_i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Display task start failed (display may not be connected)");
    }

    // Start MQTT task
    ret = mqtt_task_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MQTT task start failed");
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "All tasks started");
    ESP_LOGI(TAG, "Sensor interval: %d ms", SENSOR_READ_INTERVAL_MS);
    ESP_LOGI(TAG, "Display interval: %d ms", DISPLAY_UPDATE_INTERVAL_MS);
    ESP_LOGI(TAG, "========================================");

    // Main task has nothing more to do
    // FreeRTOS tasks are now running independently
}
