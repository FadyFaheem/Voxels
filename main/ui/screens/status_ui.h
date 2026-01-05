#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the status UI component
 */
void status_ui_init(void);

/**
 * @brief Show the status UI (device configured/connected screen)
 */
void status_ui_show(void);

/**
 * @brief Update the status UI with current connection info
 * @param connected Whether device is connected to WiFi
 * @param ip_addr IP address on the network (or NULL if not connected)
 * @param device_name Device name
 * @param wifi_ssid Connected WiFi network name
 */
void status_ui_update(bool connected, const char *ip_addr, 
                      const char *device_name, const char *wifi_ssid);

/**
 * @brief Check if status UI is currently active
 * @return true if status UI is showing
 */
bool status_ui_is_active(void);

/**
 * @brief Cleanup the status UI
 */
void status_ui_cleanup(void);

#ifdef __cplusplus
}
#endif

