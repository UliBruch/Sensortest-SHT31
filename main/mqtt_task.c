/**
 * @file mqtt_task.c
 * @brief MQTT publisher task with parallel local + cloud broker support
 *
 * Two MQTT clients run side by side:
 *   - "local": plain TCP to a LAN broker (Mosquitto)
 *   - "cloud": TLS to a public broker (HiveMQ Cloud) with username/password
 *
 * The same JSON payload is published to each connected client. If a broker is
 * not reachable, the corresponding publish is skipped (the client keeps
 * trying to reconnect in the background).
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"

#include "mqtt_task.h"
#include "mqtt_config.h"
#include "app_config.h"
#include "sensor_common.h"

static const char *TAG = "MQTT";

#define MQTT_PAYLOAD_BUF_SIZE       256

// =============================================================================
// Per-client state
// =============================================================================
typedef struct {
    const char              *name;        // log label ("local" / "cloud")
    const char              *topic;       // topic to publish on for this broker
    esp_mqtt_client_handle_t client;
    volatile bool            connected;
} mqtt_client_state_t;

static mqtt_client_state_t s_local = { .name = "local", .topic = MQTT_TOPIC };
#if MQTT_CLOUD_ENABLED
static mqtt_client_state_t s_cloud = { .name = "cloud", .topic = MQTT_CLOUD_TOPIC };
#endif

// =============================================================================
// Shared MQTT Event Handler (distinguishes clients via handler_args)
// =============================================================================
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    mqtt_client_state_t *st = (mqtt_client_state_t *)handler_args;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "[%s] connected to broker", st->name);
            st->connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "[%s] disconnected from broker", st->name);
            st->connected = false;
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "[%s] error event", st->name);
            break;
        default:
            break;
    }
}

// =============================================================================
// Payload Builder
// =============================================================================
static int build_payload(char *buf, size_t len, const sensor_state_t *s)
{
    // ISO 8601 UTC timestamp from the wall clock (set via NTP at boot).
    // If NTP hasn't synced yet, this falls back to 1970-01-01T00:00:..Z,
    // which makes the unsynced state visible on the receiver side.
    char ts_buf[32];
    time_t now = time(NULL);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

    return snprintf(buf, len,
        "{"
        "\"ts\":\"%s\","
        "\"bme\":{\"valid\":%s,\"is_bme280\":%s,\"t\":%.2f,\"h\":%.2f,\"p\":%.2f},"
        "\"sht\":{\"valid\":%s,\"t\":%.2f,\"h\":%.2f}"
        "}",
        ts_buf,
        s->bme_valid ? "true" : "false",
        s->bme_is_bme280 ? "true" : "false",
        s->bme_temperature, s->bme_humidity, s->bme_pressure,
        s->sht_valid ? "true" : "false",
        s->sht_temperature, s->sht_humidity);
}

// =============================================================================
// Publish helper - emits to one client if connected, logs the result
// =============================================================================
static void publish_one(mqtt_client_state_t *st, const char *payload, int len)
{
    if (!st->connected) {
        ESP_LOGW(TAG, "[%s] skip (not connected)", st->name);
        return;
    }
    int msg_id = esp_mqtt_client_publish(st->client, st->topic, payload, len, 0, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "[%s] publish failed", st->name);
    } else {
        ESP_LOGI(TAG, "[%s] published %d bytes", st->name, len);
    }
}

// =============================================================================
// FreeRTOS Task
// =============================================================================
static void mqtt_task(void *pvParameters)
{
    sensor_state_t local;
    char payload[MQTT_PAYLOAD_BUF_SIZE];

    ESP_LOGI(TAG, "MQTT Task started, waiting for first sensor data...");

    // Producer/consumer signaling: block until ANY sensor task has completed
    // a successful read. This ensures we don't publish zero/uninitialized data
    // on the very first cycle.
    xSemaphoreTake(g_mqtt_semaphore, portMAX_DELAY);

    ESP_LOGI(TAG, "First data available, publishing every %d ms", MQTT_PUBLISH_INTERVAL_MS);

    // xTaskDelayUntil locks the wake-up to a fixed phase reference and
    // compensates for the time spent in the body, so the publish interval
    // does not drift outwards over time.
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(MQTT_PUBLISH_INTERVAL_MS);

    while (1) {
        xTaskDelayUntil(&xLastWakeTime, xPeriod);

        // Snapshot shared state under the sensor mutex
        xSemaphoreTake(g_sensor_mutex, portMAX_DELAY);
        local = g_sensor_state;
        xSemaphoreGive(g_sensor_mutex);

        int n = build_payload(payload, sizeof(payload), &local);
        if (n <= 0 || n >= (int)sizeof(payload)) {
            ESP_LOGE(TAG, "Payload formatting failed (n=%d)", n);
            continue;
        }

        publish_one(&s_local, payload, n);
#if MQTT_CLOUD_ENABLED
        publish_one(&s_cloud, payload, n);
#endif
    }
}

// =============================================================================
// Client Setup Helpers
// =============================================================================
static esp_err_t setup_local_client(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    s_local.client = esp_mqtt_client_init(&cfg);
    if (s_local.client == NULL) {
        ESP_LOGE(TAG, "[local] init failed");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_local.client, ESP_EVENT_ANY_ID, mqtt_event_handler, &s_local));

    return esp_mqtt_client_start(s_local.client);
}

#if MQTT_CLOUD_ENABLED
static esp_err_t setup_cloud_client(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri                  = MQTT_CLOUD_URI,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username                = MQTT_CLOUD_USERNAME,
        .credentials.authentication.password = MQTT_CLOUD_PASSWORD,
    };

    s_cloud.client = esp_mqtt_client_init(&cfg);
    if (s_cloud.client == NULL) {
        ESP_LOGE(TAG, "[cloud] init failed");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_cloud.client, ESP_EVENT_ANY_ID, mqtt_event_handler, &s_cloud));

    return esp_mqtt_client_start(s_cloud.client);
}
#endif

// =============================================================================
// Public API
// =============================================================================
esp_err_t mqtt_task_start(void)
{
    esp_err_t ret = setup_local_client();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[local] start failed: %s", esp_err_to_name(ret));
        // Still continue - cloud may work even if local doesn't
    }

#if MQTT_CLOUD_ENABLED
    ret = setup_cloud_client();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[cloud] start failed: %s", esp_err_to_name(ret));
    }
#endif

    BaseType_t xReturned = xTaskCreate(
        mqtt_task,
        "mqtt_task",
        TASK_STACK_MQTT,
        NULL,
        TASK_PRIORITY_MQTT,
        NULL
    );

    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
