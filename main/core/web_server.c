#include "web_server.h"
#include "widget_manager.h"
#include "time_sync.h"
#include "font_size.h"
#include "ui_state.h"
#include "weather_service.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "sd_database.h"
#include "wifi_ap.h"
#include "cJSON.h"

static const char *TAG = "web_server";

// Web server port
#define WEB_SERVER_PORT   80
#define MAX_POST_SIZE     512

// Config values
static const char *ap_ssid = NULL;
static char device_name[64] = {0};
static char wifi_ssid[64] = {0};
static char wifi_pass[64] = {0};

// STA connection state
static volatile bool sta_connecting = false;
static volatile bool sta_connected = false;
static char sta_ip_addr[16] = {0};
static esp_netif_t *sta_netif = NULL;

// STA connection callback
static sta_connection_cb_t sta_callback = NULL;

// Flag to disable AP only on auto-connect (not during user setup)
static bool disable_ap_on_connect = false;

// Flag to track if STA event handlers are registered
static bool sta_handlers_registered = false;

// Embedded web files
// Files are in root directory - ESP-IDF converts dots to underscores in symbol names
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t setup_html_start[] asm("_binary_setup_html_start");
extern const uint8_t setup_html_end[] asm("_binary_setup_html_end");
extern const uint8_t widgets_html_start[] asm("_binary_widgets_html_start");
extern const uint8_t widgets_html_end[] asm("_binary_widgets_html_end");
extern const uint8_t settings_html_start[] asm("_binary_settings_html_start");
extern const uint8_t settings_html_end[] asm("_binary_settings_html_end");
extern const uint8_t styles_css_start[] asm("_binary_styles_css_start");
extern const uint8_t styles_css_end[] asm("_binary_styles_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");
extern const uint8_t api_js_start[] asm("_binary_api_js_start");
extern const uint8_t api_js_end[] asm("_binary_api_js_end");

// Forward declarations
static void connect_to_wifi(const char *ssid, const char *password);

// Load saved config from database
static void load_saved_config(void)
{
    if (sd_db_is_ready()) {
        sd_db_get_string("device_name", device_name, sizeof(device_name));
        sd_db_get_string("wifi_ssid", wifi_ssid, sizeof(wifi_ssid));
        sd_db_get_string("wifi_pass", wifi_pass, sizeof(wifi_pass));
        ESP_LOGI(TAG, "Loaded config - Device: %s, WiFi: %s, Pass: %s", 
                 device_name[0] ? device_name : "(not set)",
                 wifi_ssid[0] ? wifi_ssid : "(not set)",
                 wifi_pass[0] ? "(saved)" : "(not set)");
    }
}

// Replace placeholder in string
static char* replace_placeholder(const char *template, const char *placeholder, const char *value)
{
    const char *pos = strstr(template, placeholder);
    if (!pos) return strdup(template);
    
    size_t prefix_len = pos - template;
    size_t placeholder_len = strlen(placeholder);
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(pos + placeholder_len);
    
    char *result = malloc(prefix_len + value_len + suffix_len + 1);
    if (!result) return NULL;
    
    memcpy(result, template, prefix_len);
    memcpy(result + prefix_len, value, value_len);
    memcpy(result + prefix_len + value_len, pos + placeholder_len, suffix_len + 1);
    
    return result;
}


// HTTP GET handler for root path (shell HTML)
static esp_err_t root_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serving shell HTML");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

// Generic file server handler
static esp_err_t serve_file(httpd_req_t *req, const uint8_t *start, const uint8_t *end, const char *content_type)
{
    size_t len = end - start;
    // Trim null terminator if present (ESP-IDF may add one)
    if (len > 0 && start[len - 1] == '\0') {
        len--;
    }
    httpd_resp_set_type(req, content_type);
    httpd_resp_send(req, (const char *)start, len);
    return ESP_OK;
}

// Section HTML handlers
static esp_err_t section_handler(httpd_req_t *req)
{
    const char *uri = req->uri;
    ESP_LOGI(TAG, "Serving section: %s", uri);
    
    if (strstr(uri, "setup.html")) {
        // Process placeholders for setup.html
        char *html = strndup((const char *)setup_html_start, setup_html_end - setup_html_start);
        if (!html) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        
        // Replace placeholders
        struct { const char *placeholder; const char *value; } replacements[] = {
            { "%APSSID%", ap_ssid ? ap_ssid : "Voxels" },
            { "%DEVICE_NAME%", device_name[0] ? device_name : "" },
            { "%WIFI_SSID%", wifi_ssid[0] ? wifi_ssid : "" },
            { "%STORAGE%", sd_db_get_storage_type() },
        };
        
        for (int i = 0; i < sizeof(replacements) / sizeof(replacements[0]); i++) {
            char *temp = html;
            while ((temp = strstr(html, replacements[i].placeholder)) != NULL) {
                char *new_html = replace_placeholder(html, replacements[i].placeholder, replacements[i].value);
                free(html);
                html = new_html;
                if (!html) {
                    httpd_resp_send_500(req);
                    return ESP_FAIL;
                }
            }
        }
        
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, html, strlen(html));
        free(html);
        return ESP_OK;
    } else if (strstr(uri, "widgets.html")) {
        return serve_file(req, widgets_html_start, widgets_html_end, "text/html");
    } else if (strstr(uri, "settings.html")) {
        return serve_file(req, settings_html_start, settings_html_end, "text/html");
    }
    
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

// CSS handler
static esp_err_t css_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serving CSS");
    return serve_file(req, styles_css_start, styles_css_end, "text/css");
}

// JS handlers
static esp_err_t js_handler(httpd_req_t *req)
{
    const char *uri = req->uri;
    ESP_LOGI(TAG, "Serving JS: %s", uri);
    
    if (strstr(uri, "app.js")) {
        return serve_file(req, app_js_start, app_js_end, "application/javascript");
    } else if (strstr(uri, "api.js")) {
        return serve_file(req, api_js_start, api_js_end, "application/javascript");
    }
    
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

// HTTP POST handler for /api/config
static esp_err_t config_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Received config POST request");
    
    // Check content length
    if (req->content_len > MAX_POST_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }
    
    // Read request body
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int received = httpd_req_recv(req, buf, req->content_len);
    if (received <= 0) {
        free(buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    ESP_LOGI(TAG, "Received: %s", buf);
    
    // Parse JSON
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // Extract and save values
    bool saved = false;
    
    cJSON *dev_name = cJSON_GetObjectItem(json, "device_name");
    if (dev_name && cJSON_IsString(dev_name) && dev_name->valuestring[0]) {
        strncpy(device_name, dev_name->valuestring, sizeof(device_name) - 1);
        if (sd_db_is_ready()) {
            sd_db_set_string("device_name", device_name);
            saved = true;
        }
        ESP_LOGI(TAG, "Device name: %s", device_name);
    }
    
    cJSON *w_ssid = cJSON_GetObjectItem(json, "wifi_ssid");
    if (w_ssid && cJSON_IsString(w_ssid) && w_ssid->valuestring[0]) {
        strncpy(wifi_ssid, w_ssid->valuestring, sizeof(wifi_ssid) - 1);
        if (sd_db_is_ready()) {
            sd_db_set_string("wifi_ssid", wifi_ssid);
            saved = true;
        }
        ESP_LOGI(TAG, "WiFi SSID: %s", wifi_ssid);
    }
    
    cJSON *w_pass = cJSON_GetObjectItem(json, "wifi_pass");
    if (w_pass && cJSON_IsString(w_pass) && w_pass->valuestring[0]) {
        strncpy(wifi_pass, w_pass->valuestring, sizeof(wifi_pass) - 1);
        if (sd_db_is_ready()) {
            sd_db_set_string("wifi_pass", w_pass->valuestring);
            saved = true;
        }
        ESP_LOGI(TAG, "WiFi password: (saved)");
    }
    
    cJSON_Delete(json);
    
    // Save to persistent storage
    if (saved) {
        sd_db_save();
    }
    
    // Send success response first
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    
    // Connect to WiFi if credentials provided
    if (wifi_ssid[0] && wifi_pass[0]) {
        connect_to_wifi(wifi_ssid, wifi_pass);
    }
    
    return ESP_OK;
}

// HTTP GET handler for /api/config (get current config)
static esp_err_t config_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Config GET request");
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "device_name", device_name);
    cJSON_AddStringToObject(json, "wifi_ssid", wifi_ssid);
    cJSON_AddStringToObject(json, "storage", sd_db_get_storage_type());
    
    char *response = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    
    free(response);
    return ESP_OK;
}

// STA WiFi event handler
static void sta_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "STA disconnected from WiFi");
        sta_connected = false;
        sta_ip_addr[0] = '\0';
        
        // Notify callback
        if (sta_callback) {
            sta_callback(false, NULL);
        }
        
        // Try to reconnect
        if (sta_connecting) {
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(sta_ip_addr, sizeof(sta_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "STA connected! IP: %s", sta_ip_addr);
        sta_connected = true;
        sta_connecting = false;
        
        // Notify callback
        if (sta_callback) {
            sta_callback(true, sta_ip_addr);
        }
        
        // Only disable AP on auto-connect at boot, not during user setup
        if (disable_ap_on_connect && wifi_ap_is_active()) {
            ESP_LOGI(TAG, "Auto-connect complete - disabling AP");
            wifi_ap_stop();
        }
    }
}

// Connect to configured WiFi network
static void connect_to_wifi(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
    
    // Create STA netif if not exists
    if (sta_netif == NULL) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }
    
    // Register event handlers if not already registered
    if (!sta_handlers_registered) {
        ESP_LOGI(TAG, "Registering STA event handlers");
        esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                            &sta_event_handler, NULL, NULL);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &sta_event_handler, NULL, NULL);
        sta_handlers_registered = true;
    }
    
    // Set WiFi mode to AP+STA
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    
    // Configure STA
    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    
    // Start connection
    sta_connecting = true;
    sta_connected = false;
    sta_ip_addr[0] = '\0';
    
    esp_wifi_connect();
}

// HTTP GET handler for /api/status (get connection status)
static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "sta_connecting", sta_connecting);
    cJSON_AddBoolToObject(json, "sta_connected", sta_connected);
    cJSON_AddStringToObject(json, "sta_ip", sta_ip_addr);
    cJSON_AddStringToObject(json, "device_name", device_name);
    cJSON_AddStringToObject(json, "wifi_ssid", wifi_ssid);
    cJSON_AddBoolToObject(json, "setup_complete", web_server_is_setup_complete());
    
    char *response = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    
    free(response);
    return ESP_OK;
}

// HTTP GET handler for /api/scan (scan for WiFi networks)
static esp_err_t scan_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WiFi scan request");
    
    // Stop any ongoing scan first
    esp_wifi_scan_stop();
    
    // Get current WiFi mode
    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    ESP_LOGI(TAG, "Current WiFi mode: %d (AP=2, STA=1, APSTA=3)", current_mode);
    
    // Scanning requires STA interface - switch to APSTA if needed
    wifi_mode_t original_mode = current_mode;
    bool switched_mode = false;
    
    if (current_mode == WIFI_MODE_AP || current_mode == WIFI_MODE_STA) {
        // Ensure STA netif exists for scanning
        if (sta_netif == NULL) {
            ESP_LOGI(TAG, "Creating STA netif for scanning");
            sta_netif = esp_netif_create_default_wifi_sta();
        }
        
        ESP_LOGI(TAG, "Switching to APSTA mode for scan");
        esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (mode_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to switch mode: %s", esp_err_to_name(mode_err));
        } else {
            switched_mode = true;
            vTaskDelay(pdMS_TO_TICKS(200));  // Give mode switch time to complete
        }
    }
    
    // Configure and start scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 120,
        .scan_time.active.max = 500,
    };
    
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);  // Blocking scan
    
    // Retry once if scan fails
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "First scan attempt failed: %s, retrying...", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_wifi_scan_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
        ret = esp_wifi_scan_start(&scan_config, true);
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        // Restore original mode if we switched
        if (switched_mode) {
            esp_wifi_set_mode(original_mode);
        }
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    
    // Get scan results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    if (ap_count == 0) {
        ESP_LOGI(TAG, "No networks found");
        if (switched_mode) {
            esp_wifi_set_mode(original_mode);
        }
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    
    // Limit to 20 networks
    if (ap_count > 20) ap_count = 20;
    
    wifi_ap_record_t *ap_records = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!ap_records) {
        if (switched_mode) {
            esp_wifi_set_mode(original_mode);
        }
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    
    ESP_LOGI(TAG, "Found %d networks", ap_count);
    
    // Build JSON response
    cJSON *json_array = cJSON_CreateArray();
    
    for (int i = 0; i < ap_count; i++) {
        // Skip empty SSIDs (hidden networks)
        if (ap_records[i].ssid[0] == '\0') continue;
        
        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", (char *)ap_records[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(network, "auth", ap_records[i].authmode);
        cJSON_AddItemToArray(json_array, network);
    }
    
    free(ap_records);
    
    // Restore original mode if we switched
    if (switched_mode) {
        ESP_LOGI(TAG, "Restoring original WiFi mode (%d) after scan", original_mode);
        esp_wifi_set_mode(original_mode);
    }
    
    char *response = cJSON_PrintUnformatted(json_array);
    cJSON_Delete(json_array);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    
    free(response);
    return ESP_OK;
}

// Factory reset handler
static esp_err_t reset_post_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Factory reset requested!");
    
    // Clear all saved config
    if (sd_db_is_ready()) {
        sd_db_delete("device_name");
        sd_db_delete("wifi_ssid");
        sd_db_delete("wifi_pass");
        sd_db_delete("setup_complete");
        sd_db_delete("boot_count");
        sd_db_save();
        ESP_LOGI(TAG, "Cleared all saved settings");
    }
    
    // Clear local config
    device_name[0] = '\0';
    wifi_ssid[0] = '\0';
    wifi_pass[0] = '\0';
    
    // Send response before restarting
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Resetting...\"}");
    
    // Schedule restart after short delay
    ESP_LOGW(TAG, "Restarting device in 1 second...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

// Widget API handlers
static esp_err_t widgets_get_handler(httpd_req_t *req)
{
    cJSON *widgets = widget_manager_list_widgets();
    if (!widgets) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    char *response = cJSON_PrintUnformatted(widgets);
    cJSON_Delete(widgets);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

static esp_err_t widgets_active_get_handler(httpd_req_t *req)
{
    const char *active = widget_manager_get_active();
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "widget_id", active ? active : "");
    
    char *response = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

static esp_err_t widgets_active_post_handler(httpd_req_t *req)
{
    if (req->content_len > MAX_POST_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }
    
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int received = httpd_req_recv(req, buf, req->content_len);
    if (received <= 0) {
        free(buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *widget_id = cJSON_GetObjectItem(json, "widget_id");
    if (!widget_id || !cJSON_IsString(widget_id)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing widget_id");
        return ESP_FAIL;
    }
    
    esp_err_t ret = widget_manager_switch(widget_id->valuestring);
    cJSON_Delete(json);
    
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Widget not found");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// Helper to check if URI matches widget config pattern
static bool is_widget_config_uri(const char *uri, char *widget_id_out, size_t widget_id_size)
{
    const char *pattern = "/api/widgets/";
    const char *suffix = "/config";
    
    const char *start = strstr(uri, pattern);
    if (!start) {
        return false;
    }
    
    start += strlen(pattern);
    const char *end = strstr(start, suffix);
    if (!end) {
        return false;
    }
    
    size_t len = end - start;
    if (len == 0 || len >= widget_id_size) {
        return false;
    }
    
    strncpy(widget_id_out, start, len);
    widget_id_out[len] = '\0';
    return true;
}

static esp_err_t widget_config_get_handler(httpd_req_t *req)
{
    // Extract widget_id from URI: /api/widgets/{id}/config
    char widget_id[32];
    if (!is_widget_config_uri(req->uri, widget_id, sizeof(widget_id))) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    cJSON *config = widget_manager_get_config(widget_id);
    if (!config) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    char *response = cJSON_PrintUnformatted(config);
    cJSON_Delete(config);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

static esp_err_t widget_config_post_handler(httpd_req_t *req)
{
    // Extract widget_id from URI
    char widget_id[32];
    if (!is_widget_config_uri(req->uri, widget_id, sizeof(widget_id))) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    if (req->content_len > MAX_POST_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }
    
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int received = httpd_req_recv(req, buf, req->content_len);
    if (received <= 0) {
        free(buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    esp_err_t ret = widget_manager_set_config(widget_id, json);
    cJSON_Delete(json);
    
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Widget not found");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// Timezone API handlers
static esp_err_t timezone_get_handler(httpd_req_t *req)
{
    const char *tz = time_sync_get_timezone();
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "timezone", tz ? tz : "UTC0");
    
    char *response = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

static esp_err_t timezone_post_handler(httpd_req_t *req)
{
    if (req->content_len > MAX_POST_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }
    
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int received = httpd_req_recv(req, buf, req->content_len);
    if (received <= 0) {
        free(buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *tz_item = cJSON_GetObjectItem(json, "timezone");
    if (!tz_item || !cJSON_IsString(tz_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid timezone");
        return ESP_FAIL;
    }
    
    const char *tz = cJSON_GetStringValue(tz_item);
    esp_err_t ret = time_sync_set_timezone(tz);
    cJSON_Delete(json);
    
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set timezone");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// Font size API handlers
static esp_err_t font_size_get_handler(httpd_req_t *req)
{
    font_size_preset_t preset = font_size_get_preset();
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "font_size", (int)preset);
    
    char *response = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

static esp_err_t font_size_post_handler(httpd_req_t *req)
{
    if (req->content_len > MAX_POST_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }
    
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int received = httpd_req_recv(req, buf, req->content_len);
    if (received <= 0) {
        free(buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *size_item = cJSON_GetObjectItem(json, "font_size");
    if (!size_item || !cJSON_IsNumber(size_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid font_size");
        return ESP_FAIL;
    }
    
    int preset = size_item->valueint;
    if (preset < 0 || preset > 9) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Font size out of range (0-9)");
        return ESP_FAIL;
    }
    
    font_size_set_preset((font_size_preset_t)preset);
    
    // Notify UI state manager to refresh widgets
    ui_state_refresh();
    
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// Weather zip code API handlers
static esp_err_t weather_zip_get_handler(httpd_req_t *req)
{
    char zip_code[16] = {0};
    weather_service_get_zip_code(zip_code, sizeof(zip_code));
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "zip_code", zip_code);
    
    char *response = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

static esp_err_t weather_zip_post_handler(httpd_req_t *req)
{
    if (req->content_len > MAX_POST_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }
    
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int received = httpd_req_recv(req, buf, req->content_len);
    if (received <= 0) {
        free(buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *zip_item = cJSON_GetObjectItem(json, "zip_code");
    if (zip_item && cJSON_IsString(zip_item)) {
        weather_service_set_zip_code(zip_item->valuestring);
    }
    
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// Weather data API handler
static esp_err_t weather_data_get_handler(httpd_req_t *req)
{
    weather_data_t weather = {0};
    esp_err_t ret = ESP_FAIL;
    
    // Try to get cached data first
    if (weather_service_get_cached(&weather) != ESP_OK) {
        // Cache miss or expired, fetch new data
        ret = weather_service_fetch(&weather);
    } else {
        ret = ESP_OK;
    }
    
    cJSON *json = cJSON_CreateObject();
    
    if (ret == ESP_OK && weather.valid) {
        cJSON_AddNumberToObject(json, "temperature", weather.temperature);
        cJSON_AddNumberToObject(json, "humidity", weather.humidity);
        cJSON_AddNumberToObject(json, "wind_speed", weather.wind_speed);
        cJSON_AddNumberToObject(json, "weather_code", weather.weather_code);
        cJSON_AddStringToObject(json, "condition", weather.condition);
        cJSON_AddBoolToObject(json, "valid", true);
    } else {
        cJSON_AddBoolToObject(json, "valid", false);
        cJSON_AddStringToObject(json, "error", "Failed to fetch weather data");
    }
    
    char *response = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

void web_server_init(const char *ssid)
{
    ap_ssid = ssid;
    load_saved_config();
}

httpd_handle_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.max_uri_handlers = 40;  // Increased for all routes (sections, static files, API endpoints, widget configs, weather)
    
    httpd_handle_t server = NULL;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // Root page (shell HTML)
        httpd_uri_t root_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &root_uri);
        
        // Section HTML files - specific routes
        httpd_uri_t setup_section_uri = {
            .uri       = "/sections/setup.html",
            .method    = HTTP_GET,
            .handler   = section_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &setup_section_uri);
        
        httpd_uri_t widgets_section_uri = {
            .uri       = "/sections/widgets.html",
            .method    = HTTP_GET,
            .handler   = section_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &widgets_section_uri);
        
        httpd_uri_t settings_section_uri = {
            .uri       = "/sections/settings.html",
            .method    = HTTP_GET,
            .handler   = section_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &settings_section_uri);
        
        // CSS files - specific routes (ESP-IDF doesn't support wildcards)
        httpd_uri_t css_uri = {
            .uri       = "/css/styles.css",
            .method    = HTTP_GET,
            .handler   = css_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &css_uri);
        
        // JS files - specific routes
        httpd_uri_t app_js_uri = {
            .uri       = "/js/app.js",
            .method    = HTTP_GET,
            .handler   = js_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &app_js_uri);
        
        httpd_uri_t api_js_uri = {
            .uri       = "/js/api.js",
            .method    = HTTP_GET,
            .handler   = js_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_js_uri);
        
        // Config API - POST
        httpd_uri_t config_post_uri = {
            .uri       = "/api/config",
            .method    = HTTP_POST,
            .handler   = config_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &config_post_uri);
        
        // Config API - GET
        httpd_uri_t config_get_uri = {
            .uri       = "/api/config",
            .method    = HTTP_GET,
            .handler   = config_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &config_get_uri);
        
        // Scan API - GET
        httpd_uri_t scan_uri = {
            .uri       = "/api/scan",
            .method    = HTTP_GET,
            .handler   = scan_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &scan_uri);
        
        // Status API - GET
        httpd_uri_t status_uri = {
            .uri       = "/api/status",
            .method    = HTTP_GET,
            .handler   = status_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &status_uri);
        
        // Reset API - POST
        httpd_uri_t reset_uri = {
            .uri       = "/api/reset",
            .method    = HTTP_POST,
            .handler   = reset_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &reset_uri);
        
        // Timezone API - GET
        httpd_uri_t timezone_get_uri = {
            .uri       = "/api/timezone",
            .method    = HTTP_GET,
            .handler   = timezone_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &timezone_get_uri);
        
        // Timezone API - POST
        httpd_uri_t timezone_post_uri = {
            .uri       = "/api/timezone",
            .method    = HTTP_POST,
            .handler   = timezone_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &timezone_post_uri);
        
        // Font size API - GET
        httpd_uri_t font_size_get_uri = {
            .uri       = "/api/font-size",
            .method    = HTTP_GET,
            .handler   = font_size_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &font_size_get_uri);
        
        // Font size API - POST
        httpd_uri_t font_size_post_uri = {
            .uri       = "/api/font-size",
            .method    = HTTP_POST,
            .handler   = font_size_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &font_size_post_uri);
        
        // Weather zip code API - GET
        httpd_uri_t weather_zip_get_uri = {
            .uri       = "/api/weather/zip-code",
            .method    = HTTP_GET,
            .handler   = weather_zip_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &weather_zip_get_uri);
        
        // Weather zip code API - POST
        httpd_uri_t weather_zip_post_uri = {
            .uri       = "/api/weather/zip-code",
            .method    = HTTP_POST,
            .handler   = weather_zip_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &weather_zip_post_uri);
        
        // Weather data API - GET
        httpd_uri_t weather_data_get_uri = {
            .uri       = "/api/weather/data",
            .method    = HTTP_GET,
            .handler   = weather_data_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &weather_data_get_uri);
        
        // Widget API - GET list
        httpd_uri_t widgets_get_uri = {
            .uri       = "/api/widgets",
            .method    = HTTP_GET,
            .handler   = widgets_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &widgets_get_uri);
        
        // Widget API - GET active
        httpd_uri_t widgets_active_get_uri = {
            .uri       = "/api/widgets/active",
            .method    = HTTP_GET,
            .handler   = widgets_active_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &widgets_active_get_uri);
        
        // Widget API - POST active
        httpd_uri_t widgets_active_post_uri = {
            .uri       = "/api/widgets/active",
            .method    = HTTP_POST,
            .handler   = widgets_active_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &widgets_active_post_uri);
        
        // Widget config routes - register for known widgets
        // ESP-IDF doesn't support wildcards, so we register specific routes
        const char *known_widgets[] = {"clock", "timer", "weather", "calendar"};
        for (int i = 0; i < sizeof(known_widgets) / sizeof(known_widgets[0]); i++) {
            char uri_buf[64];
            
            // GET handler
            snprintf(uri_buf, sizeof(uri_buf), "/api/widgets/%s/config", known_widgets[i]);
            httpd_uri_t widget_config_get_uri = {
                .uri       = uri_buf,
                .method    = HTTP_GET,
                .handler   = widget_config_get_handler,
                .user_ctx  = NULL
            };
            httpd_register_uri_handler(server, &widget_config_get_uri);
            
            // POST handler
            httpd_uri_t widget_config_post_uri = {
                .uri       = uri_buf,
                .method    = HTTP_POST,
                .handler   = widget_config_post_handler,
                .user_ctx  = NULL
            };
            httpd_register_uri_handler(server, &widget_config_post_uri);
        }
        
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

bool web_server_is_setup_complete(void)
{
    // Setup is complete if we have both WiFi SSID and password saved
    return (wifi_ssid[0] != '\0' && wifi_pass[0] != '\0');
}

bool web_server_auto_connect(void)
{
    if (!web_server_is_setup_complete()) {
        ESP_LOGI(TAG, "Setup not complete - skipping auto-connect");
        return false;
    }
    
    ESP_LOGI(TAG, "Auto-connecting to saved WiFi: %s", wifi_ssid);
    
    // Set flag to disable AP when connection succeeds (only for auto-connect at boot)
    disable_ap_on_connect = true;
    
    connect_to_wifi(wifi_ssid, wifi_pass);
    return true;
}

bool web_server_is_sta_connected(void)
{
    return sta_connected;
}

const char* web_server_get_sta_ip(void)
{
    return sta_ip_addr;
}

const char* web_server_get_device_name(void)
{
    return device_name;
}

const char* web_server_get_wifi_ssid(void)
{
    return wifi_ssid;
}

void web_server_set_sta_callback(sta_connection_cb_t cb)
{
    sta_callback = cb;
}
