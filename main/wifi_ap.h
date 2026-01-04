#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback type for WiFi station events
 */
typedef void (*wifi_station_cb_t)(void);

/**
 * @brief Initialize WiFi AP module
 * @param on_connect Callback when a station connects
 * @param on_disconnect Callback when a station disconnects
 */
void wifi_ap_init(wifi_station_cb_t on_connect, wifi_station_cb_t on_disconnect);

/**
 * @brief Start WiFi Access Point
 * @return ESP_OK on success
 */
esp_err_t wifi_ap_start(void);

/**
 * @brief Get the generated SSID
 * @return Pointer to SSID string
 */
const char *wifi_ap_get_ssid(void);

/**
 * @brief Get the WiFi password
 * @return Pointer to password string
 */
const char *wifi_ap_get_password(void);

/**
 * @brief Get the AP IP address
 * @return Pointer to IP address string
 */
const char *wifi_ap_get_ip(void);

#ifdef __cplusplus
}
#endif

