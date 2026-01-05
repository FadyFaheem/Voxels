#include "qr_ui.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "qr_ui";

// Fade animation duration (ms)
#define QR_FADE_TIME 200

// WiFi config storage
static char wifi_ssid[32] = {0};
static char wifi_pass[32] = {0};
static char server_ip[32] = {0};

// LVGL objects
static lv_obj_t *qr_code = NULL;
static lv_obj_t *qr_container = NULL;
static lv_obj_t *info_label = NULL;
static lv_obj_t *subtitle_label = NULL;
static lv_obj_t *pass_label = NULL;
static lv_obj_t *main_container = NULL;

// QR code state
static volatile bool showing_url_qr = false;
static volatile bool transition_pending = false;
static volatile int connected_stations = 0;

// Forward declarations
static void update_qr_content(bool to_url);
static void do_qr_transition(bool to_url);
static void qr_fade_in_cb(lv_timer_t *timer);
static void clear_transition_cb(lv_timer_t *timer);

void qr_ui_init(const char *ssid, const char *password, const char *ip_addr)
{
    strncpy(wifi_ssid, ssid, sizeof(wifi_ssid) - 1);
    strncpy(wifi_pass, password, sizeof(wifi_pass) - 1);
    strncpy(server_ip, ip_addr, sizeof(server_ip) - 1);
    
    qr_code = NULL;
    qr_container = NULL;
    info_label = NULL;
    subtitle_label = NULL;
    main_container = NULL;
    showing_url_qr = false;
    transition_pending = false;
    connected_stations = 0;
}

void qr_ui_show(void)
{
    // Get active screen
    lv_obj_t *scr = lv_screen_active();
    
    // Set dark background
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    
    // Create a container for centering content
    main_container = lv_obj_create(scr);
    lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(main_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_container, 0, 0);
    lv_obj_set_style_pad_all(main_container, 20, 0);
    lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Title label (will update based on state)
    info_label = lv_label_create(main_container);
    lv_label_set_text(info_label, "Scan to Connect WiFi");
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xe94560), 0);
    
    // Create QR code container with white background
    qr_container = lv_obj_create(main_container);
    lv_obj_set_size(qr_container, 230, 230);
    lv_obj_set_style_bg_color(qr_container, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(qr_container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qr_container, 16, 0);
    lv_obj_set_style_border_width(qr_container, 0, 0);
    lv_obj_set_style_pad_all(qr_container, 15, 0);
    lv_obj_set_style_margin_top(qr_container, 20, 0);
    lv_obj_set_style_margin_bottom(qr_container, 20, 0);
    lv_obj_set_style_shadow_width(qr_container, 20, 0);
    lv_obj_set_style_shadow_color(qr_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(qr_container, LV_OPA_30, 0);
    
    // Create QR code - Initially WiFi QR code
    char wifi_qr[128];
    snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:WPA;S:%s;P:%s;;", wifi_ssid, wifi_pass);
    
    qr_code = lv_qrcode_create(qr_container);
    lv_qrcode_set_size(qr_code, 200);
    lv_qrcode_set_dark_color(qr_code, lv_color_hex(0x1a1a2e));
    lv_qrcode_set_light_color(qr_code, lv_color_hex(0xffffff));
    lv_qrcode_update(qr_code, wifi_qr, strlen(wifi_qr));
    lv_obj_center(qr_code);
    
    // Subtitle label (shows network name or URL)
    subtitle_label = lv_label_create(main_container);
    lv_label_set_text_fmt(subtitle_label, "Network: %s", wifi_ssid);
    lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x888888), 0);
    
    // Password hint (only shown for WiFi QR)
    pass_label = lv_label_create(main_container);
    lv_label_set_text_fmt(pass_label, "Password: %s", wifi_pass);
    lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pass_label, lv_color_hex(0x666666), 0);
    lv_obj_set_style_margin_top(pass_label, 5, 0);
    
    // Status indicator
    lv_obj_t *status_label = lv_label_create(main_container);
    lv_label_set_text(status_label, "Waiting for connection...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x555555), 0);
    lv_obj_set_style_margin_top(status_label, 15, 0);
    
    // Initial state
    showing_url_qr = false;
    transition_pending = false;
    
    // Check if already connected - update directly without animation
    if (connected_stations > 0) {
        update_qr_content(true);
        showing_url_qr = true;
    }
    
    ESP_LOGI(TAG, "QR UI shown");
}

bool qr_ui_is_active(void)
{
    return main_container != NULL;
}

void qr_ui_cleanup(void)
{
    if (main_container) {
        lv_obj_delete(main_container);
        main_container = NULL;
    }
    
    qr_code = NULL;
    qr_container = NULL;
    info_label = NULL;
    subtitle_label = NULL;
    pass_label = NULL;
}

void qr_ui_show_url(void)
{
    if (qr_code != NULL && !showing_url_qr && !transition_pending) {
        transition_pending = true;
        ESP_LOGI(TAG, "Switching to URL QR code");
        do_qr_transition(true);
    }
}

void qr_ui_show_wifi(void)
{
    if (qr_code != NULL && showing_url_qr && !transition_pending) {
        transition_pending = true;
        ESP_LOGI(TAG, "Switching to WiFi QR code");
        do_qr_transition(false);
    }
}

bool qr_ui_is_showing_url(void)
{
    return showing_url_qr;
}

void qr_ui_station_connected(void)
{
    connected_stations++;
    
    // Switch to URL QR code
    if (qr_code != NULL && !showing_url_qr && !transition_pending) {
        transition_pending = true;
        ESP_LOGI(TAG, "Station connected, switching to URL QR code");
        do_qr_transition(true);
    }
}

void qr_ui_station_disconnected(void)
{
    if (connected_stations > 0) {
        connected_stations--;
    }
    
    // Switch back to WiFi QR when all stations disconnect
    if (connected_stations == 0 && qr_code != NULL && showing_url_qr && !transition_pending) {
        transition_pending = true;
        ESP_LOGI(TAG, "All stations disconnected, switching to WiFi QR code");
        do_qr_transition(false);
    }
}

// Actually update the QR code content (called after fade out)
static void update_qr_content(bool to_url)
{
    ESP_LOGI(TAG, "update_qr_content called: to_url=%d", to_url);
    
    if (qr_code == NULL || info_label == NULL) {
        ESP_LOGE(TAG, "NULL pointer!");
        return;
    }
    
    if (to_url) {
        // Update QR code with URL
        char url[64];
        snprintf(url, sizeof(url), "http://%s", server_ip);
        lv_qrcode_update(qr_code, url, strlen(url));
        
        // Update labels
        lv_label_set_text(info_label, "Scan to Open Page");
        lv_obj_set_style_text_color(info_label, lv_color_hex(0x4caf50), 0);
        lv_obj_invalidate(info_label);
        
        if (subtitle_label) {
            lv_label_set_text_fmt(subtitle_label, "Visit: http://%s", server_ip);
            lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x4caf50), 0);
            lv_obj_invalidate(subtitle_label);
        }
        
        // Hide password when showing URL QR
        if (pass_label) {
            lv_obj_add_flag(pass_label, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        // WiFi QR code format: WIFI:T:WPA;S:<SSID>;P:<PASSWORD>;;
        char wifi_qr[128];
        snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:WPA;S:%s;P:%s;;", wifi_ssid, wifi_pass);
        lv_qrcode_update(qr_code, wifi_qr, strlen(wifi_qr));
        
        // Update labels
        lv_label_set_text(info_label, "Scan to Connect WiFi");
        lv_obj_set_style_text_color(info_label, lv_color_hex(0xe94560), 0);
        lv_obj_invalidate(info_label);
        
        if (subtitle_label) {
            lv_label_set_text_fmt(subtitle_label, "Network: %s", wifi_ssid);
            lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x888888), 0);
            lv_obj_invalidate(subtitle_label);
        }
        
        // Show password when showing WiFi QR
        if (pass_label) {
            lv_obj_clear_flag(pass_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Timer callback to clear transition flag
static void clear_transition_cb(lv_timer_t *timer)
{
    transition_pending = false;
    lv_timer_delete(timer);
}

// Timer callback for fade-in after content update
static void qr_fade_in_cb(lv_timer_t *timer)
{
    // Get the target state from user_data (1 = URL, 0 = WiFi)
    bool to_url = (bool)(uintptr_t)lv_timer_get_user_data(timer);
    lv_timer_delete(timer);
    
    ESP_LOGI(TAG, "Fade-in callback: updating to %s QR", to_url ? "URL" : "WiFi");
    
    // Update content while faded out
    update_qr_content(to_url);
    showing_url_qr = to_url;
    
    // Fade in
    if (qr_container) lv_obj_fade_in(qr_container, QR_FADE_TIME, 0);
    if (info_label) lv_obj_fade_in(info_label, QR_FADE_TIME, 0);
    if (subtitle_label) lv_obj_fade_in(subtitle_label, QR_FADE_TIME, 0);
    
    // Clear transition flag after fade completes
    lv_timer_create(clear_transition_cb, QR_FADE_TIME + 50, NULL);
}

// Perform QR transition with fade animation
static void do_qr_transition(bool to_url)
{
    if (qr_code == NULL) {
        transition_pending = false;
        return;
    }
    
    // Fade out current content
    if (qr_container) lv_obj_fade_out(qr_container, QR_FADE_TIME, 0);
    if (info_label) lv_obj_fade_out(info_label, QR_FADE_TIME, 0);
    if (subtitle_label) lv_obj_fade_out(subtitle_label, QR_FADE_TIME, 0);
    
    // Schedule content update and fade in, passing target state as user_data
    lv_timer_t *fade_timer = lv_timer_create((lv_timer_cb_t)qr_fade_in_cb, QR_FADE_TIME + 50, (void *)(uintptr_t)to_url);
    lv_timer_set_repeat_count(fade_timer, 1);
}

