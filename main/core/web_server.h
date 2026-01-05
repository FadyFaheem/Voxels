#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback type for STA connection events
 * @param connected true if connected, false if disconnected
 * @param ip_addr IP address (only valid when connected)
 */
typedef void (*sta_connection_cb_t)(bool connected, const char *ip_addr);

/**
 * @brief Initialize the web server module
 * @param ssid WiFi SSID to display on the web page
 */
void web_server_init(const char *ssid);

/**
 * @brief Start the HTTP web server
 * @return HTTP server handle, or NULL on failure
 */
httpd_handle_t web_server_start(void);

/**
 * @brief Stop the HTTP web server
 * @param server Server handle to stop
 * @return ESP_OK on success
 */
esp_err_t web_server_stop(httpd_handle_t server);

/**
 * @brief Check if device setup is complete (WiFi credentials saved)
 * @return true if setup is complete
 */
bool web_server_is_setup_complete(void);

/**
 * @brief Auto-connect to saved WiFi if credentials exist
 * @return true if connection attempt started
 */
bool web_server_auto_connect(void);

/**
 * @brief Check if STA is connected to WiFi
 * @return true if connected
 */
bool web_server_is_sta_connected(void);

/**
 * @brief Get the STA IP address
 * @return IP address string or empty string if not connected
 */
const char* web_server_get_sta_ip(void);

/**
 * @brief Get the saved device name
 * @return Device name or empty string if not set
 */
const char* web_server_get_device_name(void);

/**
 * @brief Get the saved WiFi SSID
 * @return WiFi SSID or empty string if not set
 */
const char* web_server_get_wifi_ssid(void);

/**
 * @brief Register callback for STA connection events
 * @param cb Callback function
 */
void web_server_set_sta_callback(sta_connection_cb_t cb);

#ifdef __cplusplus
}
#endif

