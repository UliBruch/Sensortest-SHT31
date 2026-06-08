/**
 * @file wifi_manager.h
 * @brief WiFi station mode connection manager
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stddef.h>
#include "esp_err.h"

/**
 * @brief Initialize WiFi in STA mode and connect to the configured AP.
 *
 * Blocks until either an IP is obtained or the retry limit is exhausted.
 * Credentials are taken from wifi_config.h.
 *
 * @return ESP_OK on successful connection (got IP), ESP_FAIL otherwise.
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Copy the current WiFi IP address into the caller's buffer.
 *
 * Writes "0.0.0.0" if not (yet) connected. Always null-terminated.
 *
 * @param buf  Destination buffer
 * @param len  Size of destination buffer (16 is enough for IPv4 + NUL)
 */
void wifi_manager_get_ip(char *buf, size_t len);

#endif // WIFI_MANAGER_H
