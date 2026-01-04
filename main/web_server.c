#include "web_server.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

static const char *TAG = "web_server";

// Web server port
#define WEB_SERVER_PORT   80

// SSID for web page
static const char *display_ssid = NULL;

// Embedded HTML file (from index.html)
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

// HTTP GET handler for root path
static esp_err_t root_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serving web page to client");
    
    // Calculate template size
    size_t template_len = index_html_end - index_html_start;
    
    // Generate HTML with dynamic SSID (template has %s placeholder)
    size_t html_size = template_len + 32;  // Extra space for SSID
    char *html_page = malloc(html_size);
    if (html_page == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    snprintf(html_page, html_size, (const char *)index_html_start, 
             display_ssid ? display_ssid : "Voxels");
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, strlen(html_page));
    
    free(html_page);
    return ESP_OK;
}

void web_server_init(const char *ssid)
{
    display_ssid = ssid;
}

httpd_handle_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    
    httpd_handle_t server = NULL;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &root_uri);
        ESP_LOGI(TAG, "HTTP server started on port %d", WEB_SERVER_PORT);
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
    
    return server;
}

esp_err_t web_server_stop(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Stopping HTTP server");
    return httpd_stop(server);
}

