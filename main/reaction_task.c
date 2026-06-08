/**
 * @file reaction_task.c
 * @brief Reaktionstest als FreeRTOS-Task mit State Machine
 *
 * Ablauf (State Machine):
 *
 *   IDLE      Aufforderung auf dem Display, LED aus.
 *             Tastendruck startet den Durchlauf.
 *   WAITING   Zufällige Wartezeit (WAIT_MIN_MS..WAIT_MAX_MS), LED aus.
 *             Wer hier (also vor dem LED-Signal) drückt, hat zu früh
 *             gedrückt: Das wird als Fehler "ZU FRUEH" angezeigt und der
 *             Durchlauf abgebrochen. Der Start-Tastendruck selbst wurde
 *             bereits in IDLE konsumiert; ein kurzes Settle-Fenster fängt
 *             dessen Nachprellen ab, damit es keinen Fehlalarm gibt.
 *   REACTING  LED an, Zeitmessung läuft. Der erste Tastendruck stoppt
 *             die Messung. Kommt innerhalb REACTION_TIMEOUT_MS keiner,
 *             geht das System mit einer Timeout-Meldung zurück.
 *   RESULT    Reaktionszeit groß auf dem OLED, danach zurück zu IDLE.
 *
 * Eingabe: BOOT-Taster (aktiv-low) per GPIO-Interrupt. Die ISR entprellt
 * über einen Mindestabstand und benachrichtigt den Task per Task-Notify.
 *
 * Zeitmessung: esp_timer_get_time() in Mikrosekunden, Ausgabe in ms.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "driver/gpio.h"
#include "led_strip.h"

#include "reaction_task.h"
#include "app_config.h"
#include "display.h"

static const char *TAG = "REACTION";

// =============================================================================
// Modul-Zustand
// =============================================================================
static TaskHandle_t s_task_handle = NULL;
static led_strip_handle_t s_led = NULL;
static volatile int64_t s_last_isr_us = 0;   // für die Entprellung in der ISR

// =============================================================================
// Taster-Interrupt
// =============================================================================
static void IRAM_ATTR button_isr_handler(void *arg)
{
    int64_t now = esp_timer_get_time();
    // Entprellung: Events innerhalb des Mindestabstands verwerfen.
    if (now - s_last_isr_us < (int64_t)BUTTON_DEBOUNCE_MS * 1000) {
        return;
    }
    s_last_isr_us = now;

    BaseType_t higher_prio_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task_handle, &higher_prio_woken);
    portYIELD_FROM_ISR(higher_prio_woken);
}

// =============================================================================
// LED-Steuerung (onboard WS2812)
// =============================================================================
static void led_set_go(void)
{
    // Grünes "Los!"-Signal
    led_strip_set_pixel(s_led, 0, 0, LED_BRIGHTNESS, 0);
    led_strip_refresh(s_led);
}

static void led_off(void)
{
    led_strip_clear(s_led);
}

// =============================================================================
// Notifications leeren (in IDLE/WAITING angesammelte Tastendrücke verwerfen)
// =============================================================================
static void drain_notifications(void)
{
    ulTaskNotifyTake(pdTRUE, 0);
}

// =============================================================================
// Reaktionstest-Task (State Machine)
// =============================================================================
static void reaction_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Reaktionstest-Task gestartet");

    const uint32_t wait_span_ms = WAIT_MAX_MS - WAIT_MIN_MS;

    while (1) {
        // -------------------- IDLE --------------------
        led_off();
        display_show_message("Reaktionstest", "START");
        ESP_LOGI(TAG, "IDLE - warte auf Start");

        // Auf den Start-Tastendruck warten (blockiert beliebig lange).
        drain_notifications();
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // -------------------- WAITING --------------------
        display_show_message("Achtung...", "WARTEN");

        // Kurzes Settle-Fenster: Nachprellen des Start-Tastendrucks
        // verwerfen, bevor die Früh-Erkennung beginnt.
        vTaskDelay(pdMS_TO_TICKS(150));
        drain_notifications();

        uint32_t wait_ms = WAIT_MIN_MS + (esp_random() % (wait_span_ms + 1));
        ESP_LOGI(TAG, "WAITING - %lu ms (LED aus)", (unsigned long)wait_ms);

        // Während der Zufallszeit warten. Ein Tastendruck JETZT ist zu früh.
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms)) != 0) {
            ESP_LOGI(TAG, "RESULT - zu früh gedrückt");
            led_off();
            display_show_big_text("ZU FRUEH");
            vTaskDelay(pdMS_TO_TICKS(TIMEOUT_DISPLAY_MS));
            continue;   // Durchlauf abbrechen, zurück zu IDLE
        }

        // -------------------- REACTING --------------------
        ESP_LOGI(TAG, "REACTING - LED an, Messung läuft");
        led_set_go();
        int64_t start_us = esp_timer_get_time();

        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(REACTION_TIMEOUT_MS));
        int64_t end_us = esp_timer_get_time();

        led_off();

        // -------------------- RESULT --------------------
        if (notified == 0) {
            // Timeout: niemand hat rechtzeitig gedrückt.
            ESP_LOGI(TAG, "RESULT - Timeout, kein Tastendruck");
            display_show_big_text("ZU SPAET");
            vTaskDelay(pdMS_TO_TICKS(TIMEOUT_DISPLAY_MS));
        } else {
            uint32_t reaction_ms = (uint32_t)((end_us - start_us) / 1000);
            ESP_LOGI(TAG, "RESULT - Reaktionszeit %lu ms", (unsigned long)reaction_ms);
            display_show_result(reaction_ms);
            vTaskDelay(pdMS_TO_TICKS(RESULT_DISPLAY_MS));
        }
        // zurück zu IDLE
    }
}

// =============================================================================
// Initialisierung von Taster und LED
// =============================================================================
static esp_err_t button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,   // aktiv-low: fallende Flanke = Druck
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE = bereits installiert; das ist in Ordnung.
        ESP_LOGE(TAG, "gpio_install_isr_service fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Taster an GPIO %d initialisiert", BUTTON_GPIO);
    return ESP_OK;
}

static esp_err_t led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,   // 10 MHz
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED-Init fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    led_strip_clear(s_led);
    ESP_LOGI(TAG, "WS2812-LED an GPIO %d initialisiert", LED_GPIO);
    return ESP_OK;
}

// =============================================================================
// Öffentliche API
// =============================================================================
esp_err_t reaction_task_start(void)
{
    esp_err_t ret = led_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // Den Task VOR dem Scharfschalten des Tasters erstellen: Die ISR
    // benachrichtigt s_task_handle, das hier gesetzt werden muss, bevor
    // der erste Tastendruck eintreffen kann.
    BaseType_t ok = xTaskCreate(
        reaction_task,
        "reaction_task",
        TASK_STACK_REACTION,
        NULL,
        TASK_PRIORITY_REACTION,
        &s_task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Reaktionstest-Task konnte nicht erstellt werden");
        return ESP_FAIL;
    }

    ret = button_init();
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}
