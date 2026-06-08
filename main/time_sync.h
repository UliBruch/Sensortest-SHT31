/**
 * @file time_sync.h
 * @brief SNTP-based wall clock synchronization
 */

#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialize SNTP and wait briefly for the first sync.
 *
 * Must be called only after WiFi has obtained an IP. Uses pool.ntp.org.
 *
 * @param timeout_ms  How long to wait for the initial sync. After this, the
 *                    function returns regardless; SNTP keeps running in the
 *                    background and will adjust the clock once it succeeds.
 *
 * @return ESP_OK if time was synced within the timeout,
 *         ESP_ERR_TIMEOUT otherwise (clock will catch up later).
 */
esp_err_t time_sync_start(uint32_t timeout_ms);

#endif // TIME_SYNC_H
