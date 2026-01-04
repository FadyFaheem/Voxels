#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

