/**
 * @file time_sync.c
 * @brief SNTP-based wall clock synchronization
 */

#include <time.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"

#include "time_sync.h"

static const char *TAG = "TIME";

esp_err_t time_sync_start(uint32_t timeout_ms)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t ret = esp_netif_sntp_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SNTP init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Waiting for NTP sync (up to %u ms)...", (unsigned)timeout_ms);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
        ESP_LOGW(TAG, "NTP sync timeout - clock will catch up in background");
        return ESP_ERR_TIMEOUT;
    }

    time_t now = time(NULL);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    ESP_LOGI(TAG, "Time synced: %s", buf);
    return ESP_OK;
}
