#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the QR UI module
 * @param ssid WiFi SSID to display
 * @param password WiFi password to display
 * @param ip_addr IP address for the URL QR code
 */
void qr_ui_init(const char *ssid, const char *password, const char *ip_addr);

/**
 * @brief Show the QR code UI
 */
void qr_ui_show(void);

/**
 * @brief Check if QR UI is currently shown
 * @return true if QR UI is visible
 */
bool qr_ui_is_active(void);

/**
 * @brief Delete the QR UI if it exists
 */
void qr_ui_cleanup(void);

/**
 * @brief Switch to URL QR code (call when device connects)
 */
void qr_ui_show_url(void);

/**
 * @brief Switch to WiFi QR code (call when all devices disconnect)
 */
void qr_ui_show_wifi(void);

/**
 * @brief Check if currently showing URL QR code
 * @return true if showing URL QR, false if showing WiFi QR
 */
bool qr_ui_is_showing_url(void);

/**
 * @brief Notify QR UI that a station connected
 */
void qr_ui_station_connected(void);

/**
 * @brief Notify QR UI that a station disconnected
 */
void qr_ui_station_disconnected(void);

#ifdef __cplusplus
}
#endif

