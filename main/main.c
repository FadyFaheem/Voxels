#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// Application modules
#include "core/wifi_ap.h"
#include "core/web_server.h"
#include "core/widget_manager.h"
#include "core/time_sync.h"
#include "core/ui_state.h"
#include "core/font_size.h"
#include "core/weather_service.h"
#include "sd_database.h"
#include "ui/screens/sd_format_ui.h"
#include "ui/screens/splash_ui.h"
#include "ui/screens/qr_ui.h"
#include "ui/screens/status_ui.h"
#include "ui/widgets/clock_widget.h"
#include "ui/widgets/timer_widget.h"
#include "ui/widgets/weather_widget.h"
#include "ui/widgets/calendar_widget.h"

static const char *TAG = "main";

// SD card status (stored after init, checked after splash screen)
static sd_db_status_t saved_db_status = SD_DB_NOT_PRESENT;

// Whether device setup is complete
static bool setup_complete = false;

// Callback when WiFi connection state changes
static void on_sta_connection_change(bool connected, const char *ip_addr)
{
    ESP_LOGI(TAG, "STA connection changed: %s, IP: %s", 
             connected ? "connected" : "disconnected",
             ip_addr ? ip_addr : "none");
    
    bsp_display_lock(0);
    
    // If connected and QR UI is showing, switch to widget display
    if (connected && qr_ui_is_active()) {
        ESP_LOGI(TAG, "Setup complete - switching to widget display");
        setup_complete = true;
        qr_ui_cleanup();
        status_ui_cleanup();
        
        // Initialize time sync
        time_sync_init();
        
        // Switch to saved widget (or default to clock)
        char saved_widget[32] = {0};
        if (sd_db_get_string("active_widget", saved_widget, sizeof(saved_widget)) == ESP_OK && saved_widget[0] != '\0') {
            widget_manager_switch(saved_widget);
        } else {
            widget_manager_switch("clock");
        }
    }
    // Update status UI if already active (before widget system takes over)
    else if (status_ui_is_active()) {
        status_ui_update(
            connected,
            ip_addr,
            web_server_get_device_name(),
            web_server_get_wifi_ssid()
        );
    }
    // If connected and widget system is active, ensure time sync is initialized
    else if (connected && widget_manager_get_active() != NULL) {
        if (!time_sync_is_synced()) {
            time_sync_init();
        }
    }
    
    bsp_display_unlock();
}

// Called when a WiFi station connects to our AP
static void on_station_connect(void)
{
    if (!setup_complete) {
        bsp_display_lock(0);
        qr_ui_station_connected();
        bsp_display_unlock();
    }
}

// Called when a WiFi station disconnects from our AP
static void on_station_disconnect(void)
{
    if (!setup_complete) {
        bsp_display_lock(0);
        qr_ui_station_disconnected();
        bsp_display_unlock();
    }
}

// Show the QR code setup UI (for new devices)
static void show_setup_ui(void)
{
    splash_ui_cleanup();
    sd_format_ui_cleanup();
    status_ui_cleanup();
    
    qr_ui_show();
}

// Show the status UI (for configured devices)
static void show_status_ui(void)
{
    splash_ui_cleanup();
    sd_format_ui_cleanup();
    qr_ui_cleanup();
    
    status_ui_show();
    
    // Set initial values immediately
    status_ui_update(
        web_server_is_sta_connected(),
        web_server_get_sta_ip(),
        web_server_get_device_name(),
        web_server_get_wifi_ssid()
    );
    
    // After showing status UI, restore the last active widget from database
    // Give a brief moment for status UI to be visible, then switch to widget
    vTaskDelay(pdMS_TO_TICKS(2000)); // Show status for 2 seconds
    
    bsp_display_lock(0);
    
    // Initialize time sync if not already done
    if (!time_sync_is_synced()) {
        time_sync_init();
    }
    
    // Restore saved widget from database
    char saved_widget[32] = {0};
    if (sd_db_get_string("active_widget", saved_widget, sizeof(saved_widget)) == ESP_OK && saved_widget[0] != '\0') {
        ESP_LOGI(TAG, "Restoring saved widget: %s", saved_widget);
        if (widget_manager_switch(saved_widget) == ESP_OK) {
            status_ui_cleanup(); // Hide status UI, widget is now showing
        } else {
            ESP_LOGW(TAG, "Failed to restore widget '%s', defaulting to clock", saved_widget);
            widget_manager_switch("clock");
            status_ui_cleanup();
        }
    } else {
        ESP_LOGI(TAG, "No saved widget found, defaulting to clock");
        widget_manager_switch("clock");
        status_ui_cleanup();
    }
    
    bsp_display_unlock();
}

// Called after splash screen completes
static void after_splash_complete(void)
{
    // Check if SD card needs initialization
    if (saved_db_status == SD_DB_NOT_INITIALIZED) {
        ESP_LOGI(TAG, "Showing SD card format dialog");
        sd_format_ui_show();
        return;
    }
    
    // Check if device setup is complete
    if (setup_complete) {
        ESP_LOGI(TAG, "Setup complete - showing status UI");
        show_status_ui();
    } else {
        ESP_LOGI(TAG, "Setup needed - showing QR code UI");
        show_setup_ui();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Voxels...");
    
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
            ESP_LOGW(TAG, "No SD card - using %s for storage", sd_db_get_storage_type());
            break;
            
        case SD_DB_NOT_INITIALIZED:
            ESP_LOGW(TAG, "SD card needs initialization - will prompt after splash");
            break;
            
        case SD_DB_READY:
            ESP_LOGI(TAG, "Database ready (storage: %s)", sd_db_get_storage_type());
            int boot_count = 0;
            if (sd_db_get_int("boot_count", &boot_count) == ESP_OK) {
                ESP_LOGI(TAG, "Boot count: %d", boot_count);
            }
            sd_db_set_int("boot_count", boot_count + 1);
            sd_db_save();
            break;
            
        case SD_DB_ERROR:
            ESP_LOGE(TAG, "Database error - running without storage");
            break;
    }
    
    // Initialize WiFi AP (always start AP for web access)
    wifi_ap_init(on_station_connect, on_station_disconnect);
    wifi_ap_start();
    
    // Initialize web server
    web_server_init(wifi_ap_get_ssid());
    web_server_set_sta_callback(on_sta_connection_change);
    web_server_start();
    
    // Check if setup is complete and auto-connect if so
    setup_complete = web_server_is_setup_complete();
    if (setup_complete) {
        ESP_LOGI(TAG, "Device is configured - auto-connecting to WiFi");
        web_server_auto_connect();
    } else {
        ESP_LOGI(TAG, "Device needs setup - will show QR code");
    }
    
    // Start display
    bsp_display_start();
    
    // Initialize UI state manager
    ui_state_init();
    
    // Initialize font size manager
    font_size_init();
    
    // Initialize weather service
    weather_service_init();
    
    // Initialize widget manager and register widgets
    widget_manager_init();
    widget_manager_register(&clock_widget);
    widget_manager_register(&timer_widget);
    widget_manager_register(&weather_widget);
    widget_manager_register(&calendar_widget);
    
    // Initialize UI components
    splash_ui_init(after_splash_complete);
    sd_format_ui_init(setup_complete ? show_status_ui : show_setup_ui);
    qr_ui_init(wifi_ap_get_ssid(), wifi_ap_get_password(), wifi_ap_get_ip());
    status_ui_init();
    
    // Lock display for LVGL operations
    bsp_display_lock(0);
    
    // Show splash screen with loading animation
    splash_ui_show();
    
    // Unlock display
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Application started!");
    if (!setup_complete) {
        ESP_LOGI(TAG, "Connect to WiFi '%s' with password '%s'", 
                 wifi_ap_get_ssid(), wifi_ap_get_password());
        ESP_LOGI(TAG, "Then scan the QR code or visit http://%s", wifi_ap_get_ip());
    }
}
