#include "status_ui.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "status_ui";

// LVGL objects
static lv_obj_t *main_container = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *subtitle_label = NULL;
static lv_obj_t *qr_code = NULL;
static lv_obj_t *qr_container = NULL;
static lv_obj_t *ip_label = NULL;
static lv_obj_t *wifi_label = NULL;
static lv_obj_t *status_label = NULL;

// Stored values
static char stored_ip[32] = {0};

// Colors
#define COLOR_BG        lv_color_hex(0x1a1a2e)
#define COLOR_SUCCESS   lv_color_hex(0x4caf50)
#define COLOR_WARNING   lv_color_hex(0xff9800)
#define COLOR_TEXT      lv_color_hex(0xffffff)
#define COLOR_MUTED     lv_color_hex(0x888888)
#define COLOR_ACCENT    lv_color_hex(0xe94560)

void status_ui_init(void)
{
    main_container = NULL;
    title_label = NULL;
    subtitle_label = NULL;
    qr_code = NULL;
    qr_container = NULL;
    ip_label = NULL;
    wifi_label = NULL;
    status_label = NULL;
    stored_ip[0] = '\0';
}

void status_ui_show(void)
{
    lv_obj_t *scr = lv_screen_active();
    
    // Set dark background
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    
    // Create main container
    main_container = lv_obj_create(scr);
    lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(main_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_container, 0, 0);
    lv_obj_set_style_pad_all(main_container, 20, 0);
    lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(main_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title - "Configured!"
    title_label = lv_label_create(main_container);
    lv_label_set_text(title_label, "Scan to Open Page");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title_label, COLOR_SUCCESS, 0);
    
    // QR code container with white background
    qr_container = lv_obj_create(main_container);
    lv_obj_set_size(qr_container, 200, 200);
    lv_obj_set_style_bg_color(qr_container, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(qr_container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qr_container, 16, 0);
    lv_obj_set_style_border_width(qr_container, 0, 0);
    lv_obj_set_style_pad_all(qr_container, 10, 0);
    lv_obj_set_style_margin_top(qr_container, 15, 0);
    lv_obj_set_style_margin_bottom(qr_container, 15, 0);
    lv_obj_set_style_shadow_width(qr_container, 20, 0);
    lv_obj_set_style_shadow_color(qr_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(qr_container, LV_OPA_30, 0);
    lv_obj_clear_flag(qr_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create QR code - initially empty, will be updated with IP
    qr_code = lv_qrcode_create(qr_container);
    lv_qrcode_set_size(qr_code, 180);
    lv_qrcode_set_dark_color(qr_code, lv_color_hex(0x1a1a2e));
    lv_qrcode_set_light_color(qr_code, lv_color_hex(0xffffff));
    lv_qrcode_update(qr_code, "http://192.168.4.1", strlen("http://192.168.4.1"));
    lv_obj_center(qr_code);
    
    // IP Address label
    ip_label = lv_label_create(main_container);
    lv_label_set_text(ip_label, "Connecting...");
    lv_obj_set_style_text_font(ip_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ip_label, COLOR_SUCCESS, 0);
    
    // WiFi info
    wifi_label = lv_label_create(main_container);
    lv_label_set_text(wifi_label, "WiFi: -");
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_label, COLOR_MUTED, 0);
    lv_obj_set_style_margin_top(wifi_label, 10, 0);
    
    // Status label
    status_label = lv_label_create(main_container);
    lv_label_set_text(status_label, "Connecting to network...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, COLOR_WARNING, 0);
    lv_obj_set_style_margin_top(status_label, 5, 0);
    
    ESP_LOGI(TAG, "Status UI shown");
}

void status_ui_update(bool connected, const char *ip_addr, 
                      const char *device_name, const char *wifi_ssid)
{
    if (!main_container) return;
    
    bsp_display_lock(0);
    
    if (connected && ip_addr && ip_addr[0]) {
        // Update IP label
        if (ip_label) {
            lv_label_set_text(ip_label, ip_addr);
        }
        
        // Update QR code with URL if IP changed
        if (qr_code && strcmp(stored_ip, ip_addr) != 0) {
            strncpy(stored_ip, ip_addr, sizeof(stored_ip) - 1);
            char url[64];
            snprintf(url, sizeof(url), "http://%s", ip_addr);
            lv_qrcode_update(qr_code, url, strlen(url));
            ESP_LOGI(TAG, "QR code updated: %s", url);
        }
        
        // Update status
        if (status_label) {
            lv_label_set_text(status_label, LV_SYMBOL_OK " Connected & Ready");
            lv_obj_set_style_text_color(status_label, COLOR_SUCCESS, 0);
        }
        
        if (title_label) {
            lv_label_set_text(title_label, "Scan to Open Page");
            lv_obj_set_style_text_color(title_label, COLOR_SUCCESS, 0);
        }
    } else {
        // Not connected yet
        if (ip_label) {
            lv_label_set_text(ip_label, "Connecting...");
        }
        
        if (status_label) {
            lv_label_set_text(status_label, LV_SYMBOL_REFRESH " Connecting to network...");
            lv_obj_set_style_text_color(status_label, COLOR_WARNING, 0);
        }
        
        if (title_label) {
            lv_label_set_text(title_label, "Connecting...");
            lv_obj_set_style_text_color(title_label, COLOR_WARNING, 0);
        }
    }
    
    // Update WiFi name
    if (wifi_label && wifi_ssid && wifi_ssid[0]) {
        char wifi_text[64];
        snprintf(wifi_text, sizeof(wifi_text), "WiFi: %s", wifi_ssid);
        lv_label_set_text(wifi_label, wifi_text);
    }
    
    bsp_display_unlock();
}

bool status_ui_is_active(void)
{
    return main_container != NULL;
}

void status_ui_cleanup(void)
{
    if (main_container) {
        lv_obj_delete(main_container);
        main_container = NULL;
    }
    
    title_label = NULL;
    subtitle_label = NULL;
    qr_code = NULL;
    qr_container = NULL;
    ip_label = NULL;
    wifi_label = NULL;
    status_label = NULL;
    stored_ip[0] = '\0';
}
