#include "wifi_ap.h"
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_netif.h"

static const char *TAG = "wifi_ap";

// WiFi AP Configuration
#define WIFI_AP_SSID_PREFIX  "Voxels-"
#define WIFI_AP_PASS         "voxels123"
#define WIFI_AP_CHANNEL      1
#define WIFI_AP_MAX_CONN     4
#define AP_IP_ADDR           "192.168.4.1"

// Generated SSID
static char wifi_ssid[32] = {0};

// AP state
static bool ap_active = false;

// Callbacks
static wifi_station_cb_t connect_callback = NULL;
static wifi_station_cb_t disconnect_callback = NULL;

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station %02X:%02X:%02X:%02X:%02X:%02X joined, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
        
        if (connect_callback) {
            connect_callback();
        }
        
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station %02X:%02X:%02X:%02X:%02X:%02X left, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
        
        if (disconnect_callback) {
            disconnect_callback();
        }
    }
}

// Generate unique SSID from MAC address
static void generate_unique_ssid(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    
    // Create SSID like "Voxels-A1B2C3" using last 3 bytes of MAC
    snprintf(wifi_ssid, sizeof(wifi_ssid), "%s%02X%02X%02X", 
             WIFI_AP_SSID_PREFIX, mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Generated unique SSID: %s", wifi_ssid);
}

void wifi_ap_init(wifi_station_cb_t on_connect, wifi_station_cb_t on_disconnect)
{
    connect_callback = on_connect;
    disconnect_callback = on_disconnect;
}

esp_err_t wifi_ap_start(void)
{
    ESP_LOGI(TAG, "Starting WiFi AP...");
    
    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create default WiFi AP
    esp_netif_create_default_wifi_ap();
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Generate unique SSID based on MAC address
    generate_unique_ssid();
    
    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL, NULL));
    
    // Configure AP
    wifi_config_t wifi_config = {
        .ap = {
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASS,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    
    // Copy the generated SSID
    memcpy(wifi_config.ap.ssid, wifi_ssid, strlen(wifi_ssid));
    wifi_config.ap.ssid_len = strlen(wifi_ssid);
    
    // If password is empty, use open authentication
    if (strlen(WIFI_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ap_active = true;
    
    ESP_LOGI(TAG, "WiFi AP started. SSID: %s, Password: %s", wifi_ssid, WIFI_AP_PASS);
    ESP_LOGI(TAG, "Connect to this network and visit http://%s", AP_IP_ADDR);
    
    return ESP_OK;
}

const char *wifi_ap_get_ssid(void)
{
    return wifi_ssid;
}

const char *wifi_ap_get_password(void)
{
    return WIFI_AP_PASS;
}

const char *wifi_ap_get_ip(void)
{
    return AP_IP_ADDR;
}

esp_err_t wifi_ap_stop(void)
{
    if (!ap_active) {
        ESP_LOGW(TAG, "AP already stopped");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping WiFi AP - switching to STA only mode");
    
    // Get current mode
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    
    if (mode == WIFI_MODE_APSTA) {
        // Switch to STA only
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    } else if (mode == WIFI_MODE_AP) {
        // Just stop WiFi (will be restarted if needed)
        ESP_ERROR_CHECK(esp_wifi_stop());
    }
    
    ap_active = false;
    ESP_LOGI(TAG, "WiFi AP stopped");
    
    return ESP_OK;
}

bool wifi_ap_is_active(void)
{
    return ap_active;
}

