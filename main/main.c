#include "esp_log.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// Application modules
#include "wifi_ap.h"
#include "web_server.h"
#include "sd_database.h"
#include "sd_format_ui.h"
#include "splash_ui.h"
#include "qr_ui.h"

static const char *TAG = "main";

// SD card status (stored after init, checked after splash screen)
static sd_db_status_t saved_db_status = SD_DB_NOT_PRESENT;

// Called when a WiFi station connects
static void on_station_connect(void)
{
    bsp_display_lock(0);
    qr_ui_station_connected();
    bsp_display_unlock();
}

// Called when a WiFi station disconnects
static void on_station_disconnect(void)
{
    bsp_display_lock(0);
    qr_ui_station_disconnected();
    bsp_display_unlock();
}

// Called when we need to show the main QR code UI
static void show_main_ui(void)
{
    // Cleanup any previous screens
    splash_ui_cleanup();
    sd_format_ui_cleanup();
    
    // Show QR code UI
    qr_ui_show();
}

// Called after splash screen completes
static void after_splash_complete(void)
{
    // Check if SD card needs initialization
    if (saved_db_status == SD_DB_NOT_INITIALIZED) {
        ESP_LOGI(TAG, "Showing SD card format dialog");
        sd_format_ui_show();
    } else {
        // Go directly to QR code UI
        show_main_ui();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Voxels QR Web Server...");
    
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize SD card database (save status for after splash screen)
    saved_db_status = sd_db_init();
    
    switch (saved_db_status) {
        case SD_DB_NOT_PRESENT:
            ESP_LOGW(TAG, "No SD card detected - running without database");
            break;
            
        case SD_DB_NOT_INITIALIZED:
            ESP_LOGW(TAG, "SD card needs initialization - will prompt after splash");
            break;
            
        case SD_DB_READY:
            ESP_LOGI(TAG, "SD card database ready");
            // Example: Read a saved value
            int boot_count = 0;
            if (sd_db_get_int("boot_count", &boot_count) == ESP_OK) {
                ESP_LOGI(TAG, "Boot count: %d", boot_count);
            }
            // Increment and save boot count
            sd_db_set_int("boot_count", boot_count + 1);
            sd_db_save();
            break;
            
        case SD_DB_ERROR:
            ESP_LOGE(TAG, "SD card database error - running without database");
            break;
    }
    
    // Initialize and start WiFi AP
    wifi_ap_init(on_station_connect, on_station_disconnect);
    wifi_ap_start();
    
    // Initialize and start web server
    web_server_init(wifi_ap_get_ssid());
    web_server_start();
    
    // Start display
    bsp_display_start();
    
    // Initialize UI components
    splash_ui_init(after_splash_complete);
    sd_format_ui_init(show_main_ui);
    qr_ui_init(wifi_ap_get_ssid(), wifi_ap_get_password(), wifi_ap_get_ip());
    
    // Lock display for LVGL operations
    bsp_display_lock(0);
    
    // Show splash screen with loading animation
    splash_ui_show();
    
    // Unlock display
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Application started successfully!");
    ESP_LOGI(TAG, "Connect to WiFi '%s' with password '%s'", 
             wifi_ap_get_ssid(), wifi_ap_get_password());
    ESP_LOGI(TAG, "Then scan the QR code or visit http://%s", wifi_ap_get_ip());
}
