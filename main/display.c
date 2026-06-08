/**
 * @file display.c
 * @brief SSD1306-OLED-Treiber für den Reaktionstest
 *
 * Minimaler SSD1306-Treiber für 128x32-OLEDs über die ESP-IDF-I2C-API.
 * Enthält eine 5x7-Font, die für die Ergebnisanzeige um einen frei
 * wählbaren Faktor hochskaliert werden kann (jeder Pixel als NxN-Block).
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "display.h"
#include "app_config.h"

static const char *TAG = "DISPLAY";

// =============================================================================
// SSD1306-Kommandos
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

// Geometrie der Font: 5 Spalten breit, 7 Zeilen hoch, +1 Spalte Abstand
#define FONT_WIDTH      5
#define FONT_HEIGHT     7
#define FONT_ADVANCE    6   // FONT_WIDTH + 1 Pixel Abstand

// =============================================================================
// Einfache 5x7-Font (ASCII 32-127)
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
    {0x08, 0x1C, 0x2A, 0x08, 0x08}, // DEL (Pfeil rechts)
};

// =============================================================================
// Modul-Zustand
// =============================================================================
static i2c_master_dev_handle_t s_dev_handle = NULL;
static uint8_t s_framebuffer[DISPLAY_WIDTH * DISPLAY_PAGES];

// =============================================================================
// SSD1306-Low-Level
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
// Framebuffer-Zeichnen
// =============================================================================
static void fb_clear(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

static inline void fb_set_pixel(int x, int y)
{
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
        return;
    }
    int page = y / 8;
    int bit = y % 8;
    s_framebuffer[page * DISPLAY_WIDTH + x] |= (1 << bit);
}

/**
 * Zeichnet ein Zeichen mit Skalierungsfaktor: Jeder gesetzte Font-Pixel
 * wird als (scale x scale)-Block gezeichnet. scale == 1 ergibt die
 * Originalgröße 5x7.
 */
static void fb_draw_char_scaled(int x, int y, char c, int scale)
{
    if (c < 32 || c > 127) {
        c = '?';
    }
    int idx = c - 32;

    for (int col = 0; col < FONT_WIDTH; col++) {
        uint8_t glyph_col = font5x7[idx][col];
        for (int row = 0; row < FONT_HEIGHT; row++) {
            if (glyph_col & (1 << row)) {
                // Pixel als scale x scale Block setzen
                for (int dx = 0; dx < scale; dx++) {
                    for (int dy = 0; dy < scale; dy++) {
                        fb_set_pixel(x + col * scale + dx, y + row * scale + dy);
                    }
                }
            }
        }
    }
}

static void fb_draw_string_scaled(int x, int y, const char *str, int scale)
{
    while (*str) {
        fb_draw_char_scaled(x, y, *str, scale);
        x += FONT_ADVANCE * scale;
        str++;
    }
}

// Pixelbreite eines Strings bei gegebenem Skalierungsfaktor.
static int fb_string_width(const char *str, int scale)
{
    int len = (int)strlen(str);
    if (len == 0) {
        return 0;
    }
    // n Zeichen belegen (n-1) volle Advances + die Breite des letzten Glyphs.
    return (len - 1) * FONT_ADVANCE * scale + FONT_WIDTH * scale;
}

// Zeichnet einen String horizontal und vertikal zentriert.
static void fb_draw_string_centered(const char *str, int scale)
{
    int w = fb_string_width(str, scale);
    int h = FONT_HEIGHT * scale;
    int x = (DISPLAY_WIDTH - w) / 2;
    int y = (DISPLAY_HEIGHT - h) / 2;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    fb_draw_string_scaled(x, y, str, scale);
}

static esp_err_t fb_update_display(void)
{
    ssd1306_send_command(SSD1306_CMD_SET_COL_ADDR);
    ssd1306_send_command(0);
    ssd1306_send_command(127);
    ssd1306_send_command(SSD1306_CMD_SET_PAGE_ADDR);
    ssd1306_send_command(0);
    ssd1306_send_command(3);

    return ssd1306_send_data(s_framebuffer, sizeof(s_framebuffer));
}

// =============================================================================
// Öffentliche API
// =============================================================================
esp_err_t display_init(i2c_master_bus_handle_t bus_handle)
{
    esp_err_t ret;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SSD1306_I2C_ADDR,
        .scl_speed_hz = APP_I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gerät konnte nicht zum I2C-Bus hinzugefügt werden: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    // Initialisierungssequenz für 128x32-Display
    const uint8_t init_cmds[] = {
        SSD1306_CMD_DISPLAY_OFF,
        SSD1306_CMD_SET_CLK_DIV, 0x80,
        SSD1306_CMD_SET_MUX_RATIO, 0x1F,        // Multiplex-Ratio (32-1)
        SSD1306_CMD_SET_DISPLAY_OFFSET, 0x00,
        SSD1306_CMD_SET_START_LINE | 0x00,
        SSD1306_CMD_SET_CHARGE_PUMP, 0x14,      // Charge Pump an
        SSD1306_CMD_SET_MEMORY_MODE, 0x00,      // horizontaler Adressmodus
        SSD1306_CMD_SET_SEG_REMAP,
        SSD1306_CMD_SET_COM_SCAN_DEC,
        SSD1306_CMD_SET_COM_PINS, 0x02,         // COM-Pins für 128x32
        SSD1306_CMD_SET_CONTRAST, 0x8F,
        SSD1306_CMD_ENTIRE_DISPLAY_RAM,
        SSD1306_CMD_SET_NORMAL_DISPLAY,
        SSD1306_CMD_DISPLAY_ON
    };

    ret = ssd1306_send_commands(init_cmds, sizeof(init_cmds));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display-Init fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    fb_clear();
    ret = fb_update_display();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display konnte nicht gelöscht werden: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SSD1306 128x32 initialisiert");
    return ESP_OK;
}

void display_show_message(const char *line1, const char *line2)
{
    fb_clear();
    if (line1) {
        // obere Zeile zentrieren, kleine Schrift
        int x = (DISPLAY_WIDTH - fb_string_width(line1, 1)) / 2;
        if (x < 0) {
            x = 0;
        }
        fb_draw_string_scaled(x, 2, line1, 1);
    }
    if (line2) {
        // untere Zeile zentrieren, etwas größer für die Aufforderung
        int x = (DISPLAY_WIDTH - fb_string_width(line2, 2)) / 2;
        if (x < 0) {
            x = 0;
        }
        fb_draw_string_scaled(x, 14, line2, 2);
    }
    fb_update_display();
}

void display_show_result(uint32_t millis)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)millis);

    fb_clear();
    // So groß wie möglich: Skalierung 4 nutzen, wenn es passt, sonst 3.
    int scale = 4;
    if (fb_string_width(buf, scale) > DISPLAY_WIDTH ||
        FONT_HEIGHT * scale > DISPLAY_HEIGHT) {
        scale = 3;
    }
    fb_draw_string_centered(buf, scale);
    fb_update_display();
}

void display_show_big_text(const char *text)
{
    fb_clear();
    int scale = 3;
    if (fb_string_width(text, scale) > DISPLAY_WIDTH) {
        scale = 2;
    }
    fb_draw_string_centered(text, scale);
    fb_update_display();
}
