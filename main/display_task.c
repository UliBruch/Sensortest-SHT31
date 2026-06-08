/**
 * @file display_task.c
 * @brief SSD1306 OLED display driver and FreeRTOS task implementation
 *
 * Minimal SSD1306 driver for 128x32 OLED displays using ESP-IDF I2C API.
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "display_task.h"
#include "app_config.h"
#include "sensor_common.h"
#include "wifi_manager.h"

static const char *TAG = "DISPLAY";

// =============================================================================
// SSD1306 Commands
// =============================================================================
#define SSD1306_CMD_DISPLAY_OFF         0xAE
#define SSD1306_CMD_DISPLAY_ON          0xAF
#define SSD1306_CMD_SET_MUX_RATIO       0xA8
#define SSD1306_CMD_SET_DISPLAY_OFFSET  0xD3
#define SSD1306_CMD_SET_START_LINE      0x40
#define SSD1306_CMD_SET_SEG_REMAP       0xA1
#define SSD1306_CMD_SET_COM_SCAN_DEC    0xC8
#define SSD1306_CMD_SET_COM_PINS        0xDA
#define SSD1306_CMD_SET_CONTRAST        0x81
#define SSD1306_CMD_ENTIRE_DISPLAY_RAM  0xA4
#define SSD1306_CMD_SET_NORMAL_DISPLAY  0xA6
#define SSD1306_CMD_SET_CLK_DIV         0xD5
#define SSD1306_CMD_SET_CHARGE_PUMP     0x8D
#define SSD1306_CMD_SET_MEMORY_MODE     0x20
#define SSD1306_CMD_SET_COL_ADDR        0x21
#define SSD1306_CMD_SET_PAGE_ADDR       0x22

#define SSD1306_CONTROL_CMD_SINGLE      0x80
#define SSD1306_CONTROL_CMD_STREAM      0x00
#define SSD1306_CONTROL_DATA            0x40

// =============================================================================
// Simple 5x7 Font (ASCII 32-127)
// =============================================================================
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x00, 0x08, 0x14, 0x22, 0x41}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x41, 0x22, 0x14, 0x08, 0x00}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x01, 0x01}, // F
    {0x3E, 0x41, 0x41, 0x51, 0x32}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x00, 0x7F, 0x41, 0x41}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x41, 0x41, 0x7F, 0x00, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x08, 0x14, 0x54, 0x54, 0x3C}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x00, 0x7F, 0x10, 0x28, 0x44}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // }
    {0x08, 0x08, 0x2A, 0x1C, 0x08}, // ~
    {0x08, 0x1C, 0x2A, 0x08, 0x08}, // DEL (arrow right)
};

// =============================================================================
// Module State
// =============================================================================
static i2c_master_dev_handle_t s_dev_handle = NULL;
static uint8_t s_framebuffer[DISPLAY_WIDTH * DISPLAY_PAGES];

// =============================================================================
// SSD1306 Low-Level Functions
// =============================================================================
static esp_err_t ssd1306_send_command(uint8_t cmd)
{
    uint8_t buf[2] = {SSD1306_CONTROL_CMD_SINGLE, cmd};
    return i2c_master_transmit(s_dev_handle, buf, 2, APP_I2C_TIMEOUT_MS);
}

static esp_err_t ssd1306_send_commands(const uint8_t *cmds, size_t len)
{
    esp_err_t ret = ESP_OK;
    for (size_t i = 0; i < len && ret == ESP_OK; i++) {
        ret = ssd1306_send_command(cmds[i]);
    }
    return ret;
}

static esp_err_t ssd1306_send_data(const uint8_t *data, size_t len)
{
    // Buffer for control byte + data
    uint8_t *buf = malloc(len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    buf[0] = SSD1306_CONTROL_DATA;
    memcpy(buf + 1, data, len);

    esp_err_t ret = i2c_master_transmit(s_dev_handle, buf, len + 1, APP_I2C_TIMEOUT_MS);
    free(buf);

    return ret;
}

// =============================================================================
// Display Initialization
// =============================================================================
static esp_err_t ssd1306_init(i2c_master_bus_handle_t bus_handle)
{
    esp_err_t ret;

    // Add device to I2C bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SSD1306_I2C_ADDR,
        .scl_speed_hz = APP_I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialization sequence for 128x32 display
    const uint8_t init_cmds[] = {
        SSD1306_CMD_DISPLAY_OFF,
        SSD1306_CMD_SET_CLK_DIV, 0x80,          // Clock divide ratio
        SSD1306_CMD_SET_MUX_RATIO, 0x1F,        // Multiplex ratio (32-1)
        SSD1306_CMD_SET_DISPLAY_OFFSET, 0x00,   // Display offset
        SSD1306_CMD_SET_START_LINE | 0x00,      // Start line 0
        SSD1306_CMD_SET_CHARGE_PUMP, 0x14,      // Enable charge pump
        SSD1306_CMD_SET_MEMORY_MODE, 0x00,      // Horizontal addressing mode
        SSD1306_CMD_SET_SEG_REMAP,              // Segment remap
        SSD1306_CMD_SET_COM_SCAN_DEC,           // COM scan direction
        SSD1306_CMD_SET_COM_PINS, 0x02,         // COM pins config for 128x32
        SSD1306_CMD_SET_CONTRAST, 0x8F,         // Contrast
        SSD1306_CMD_ENTIRE_DISPLAY_RAM,         // Display from RAM
        SSD1306_CMD_SET_NORMAL_DISPLAY,         // Normal display (not inverted)
        SSD1306_CMD_DISPLAY_ON
    };

    ret = ssd1306_send_commands(init_cmds, sizeof(init_cmds));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Clear framebuffer and display
    memset(s_framebuffer, 0, sizeof(s_framebuffer));

    // Set column and page address
    ssd1306_send_command(SSD1306_CMD_SET_COL_ADDR);
    ssd1306_send_command(0);    // Start column
    ssd1306_send_command(127);  // End column
    ssd1306_send_command(SSD1306_CMD_SET_PAGE_ADDR);
    ssd1306_send_command(0);    // Start page
    ssd1306_send_command(3);    // End page (4 pages for 32px height)

    // Clear display
    ssd1306_send_data(s_framebuffer, sizeof(s_framebuffer));

    ESP_LOGI(TAG, "SSD1306 128x32 initialized");

    return ESP_OK;
}

// =============================================================================
// Framebuffer Drawing Functions
// =============================================================================
static void fb_clear(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

static void fb_draw_char(int x, int y, char c)
{
    if (c < 32 || c > 127) {
        c = '?';
    }

    int idx = c - 32;

    for (int col = 0; col < 5; col++) {
        int px = x + col;
        if (px >= DISPLAY_WIDTH) break;

        uint8_t glyph_col = font5x7[idx][col];

        for (int row = 0; row < 7; row++) {
            int py = y + row;
            if (py >= DISPLAY_HEIGHT) break;

            if (glyph_col & (1 << row)) {
                // Set pixel
                int page = py / 8;
                int bit = py % 8;
                s_framebuffer[page * DISPLAY_WIDTH + px] |= (1 << bit);
            }
        }
    }
}

static void fb_draw_string(int x, int y, const char *str)
{
    while (*str) {
        fb_draw_char(x, y, *str);
        x += 6;  // 5 pixel width + 1 pixel spacing
        str++;
    }
}

static esp_err_t fb_update_display(void)
{
    // Set address range
    ssd1306_send_command(SSD1306_CMD_SET_COL_ADDR);
    ssd1306_send_command(0);
    ssd1306_send_command(127);
    ssd1306_send_command(SSD1306_CMD_SET_PAGE_ADDR);
    ssd1306_send_command(0);
    ssd1306_send_command(3);

    return ssd1306_send_data(s_framebuffer, sizeof(s_framebuffer));
}

// =============================================================================
// FreeRTOS Task
// =============================================================================
static void display_task(void *pvParameters)
{
    char line[22];  // Max 21 chars at 6px width for 128px display
    sensor_state_t local;

    ESP_LOGI(TAG, "Display Task started");

    // Splash screen - shown on boot and on every reset
    fb_clear();
    fb_draw_string(0,  0, "ESP32-C6 Sensors");
    fb_draw_string(0,  8, "----------------");
    fb_draw_string(0, 16, "Booting...");
    fb_draw_string(0, 24, "BME280 SHT31 OLED");
    fb_update_display();
    vTaskDelay(pdMS_TO_TICKS(3000));

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS));

        // Copy sensor state with mutex protection
        xSemaphoreTake(g_sensor_mutex, portMAX_DELAY);
        local = g_sensor_state;
        xSemaphoreGive(g_sensor_mutex);

        // Clear framebuffer
        fb_clear();

        // Line 0: BME temperature and pressure
        if (local.bme_valid) {
            snprintf(line, sizeof(line), "BME:%.1fC %.0fhPa",
                     local.bme_temperature, local.bme_pressure);
        } else {
            snprintf(line, sizeof(line), "BME: --");
        }
        fb_draw_string(0, 0, line);

        // Line 1: BME humidity (if available)
        if (local.bme_valid && local.bme_is_bme280 && local.bme_humidity >= 0) {
            snprintf(line, sizeof(line), "    Hum:%.0f%%", local.bme_humidity);
        } else if (local.bme_valid) {
            snprintf(line, sizeof(line), "    (no hum)");
        } else {
            line[0] = '\0';
        }
        fb_draw_string(0, 8, line);

        // Line 2: SHT31 temperature and humidity
        if (local.sht_valid) {
            snprintf(line, sizeof(line), "SHT:%.1fC %.0f%%",
                     local.sht_temperature, local.sht_humidity);
        } else {
            snprintf(line, sizeof(line), "SHT: --");
        }
        fb_draw_string(0, 16, line);

        // Line 3: WiFi IP address
        char ip[16];
        wifi_manager_get_ip(ip, sizeof(ip));
        snprintf(line, sizeof(line), "IP:%s", ip);
        fb_draw_string(0, 24, line);

        // Update display
        fb_update_display();
    }
}

// =============================================================================
// Public API
// =============================================================================
esp_err_t display_task_start(i2c_master_bus_handle_t i2c_bus_handle)
{
    esp_err_t ret = ssd1306_init(i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed");
        return ret;
    }

    BaseType_t xReturned = xTaskCreate(
        display_task,
        "display_task",
        TASK_STACK_DISPLAY,
        NULL,
        TASK_PRIORITY_DISPLAY,
        NULL
    );

    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
