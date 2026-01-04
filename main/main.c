#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "sd_database.h"

static const char *TAG = "main";

// WiFi AP Configuration
#define WIFI_AP_SSID_PREFIX  "Voxels-"
#define WIFI_AP_PASS         "voxels123"
#define WIFI_AP_CHANNEL      1
#define WIFI_AP_MAX_CONN     4

// Generated SSID (will be populated at runtime)
static char wifi_ap_ssid[32] = {0};

// Web server port
#define WEB_SERVER_PORT   80

// The IP address ESP32 will have in AP mode
#define AP_IP_ADDR        "192.168.4.1"

// LVGL objects
static lv_obj_t *qr_code = NULL;
static lv_obj_t *qr_container = NULL;
static lv_obj_t *info_label = NULL;
static lv_obj_t *subtitle_label = NULL;
static lv_obj_t *main_container = NULL;

// Splash screen objects
static lv_obj_t *splash_screen = NULL;
static lv_obj_t *loading_bar = NULL;
static lv_timer_t *loading_timer = NULL;
static int loading_progress = 0;

// Format dialog objects
static lv_obj_t *format_dialog = NULL;
static volatile bool format_confirmed = false;
static volatile bool format_cancelled = false;

// SD card status (stored after init, checked after splash screen)
static sd_db_status_t saved_db_status = SD_DB_NOT_PRESENT;

// QR code state: false = WiFi QR, true = URL QR (volatile for thread safety)
static volatile bool showing_url_qr = false;
static volatile bool transition_pending = false;
static volatile bool pending_switch_to_url = false;
static volatile int connected_stations = 0;

// Fade animation duration (ms)
#define QR_FADE_TIME 200

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
    
    snprintf(html_page, html_size, (const char *)index_html_start, wifi_ap_ssid);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, strlen(html_page));
    
    free(html_page);
    return ESP_OK;
}

// Start HTTP server
static httpd_handle_t start_webserver(void)
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
    }
    
    return server;
}

// Forward declaration
static void do_qr_transition(bool to_url);

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station %02X:%02X:%02X:%02X:%02X:%02X joined, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
        
        connected_stations++;
        
        // Switch to URL QR code (only if not already showing and not transitioning)
        if (qr_code != NULL && !showing_url_qr && !transition_pending) {
            transition_pending = true;
            pending_switch_to_url = true;
            ESP_LOGI(TAG, "Switching to URL QR code");
            bsp_display_lock(0);
            do_qr_transition(true);
            bsp_display_unlock();
        }
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station %02X:%02X:%02X:%02X:%02X:%02X left, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
        
        if (connected_stations > 0) {
            connected_stations--;
        }
        
        // Switch back to WiFi QR when all stations disconnect
        if (connected_stations == 0 && qr_code != NULL && showing_url_qr && !transition_pending) {
            transition_pending = true;
            pending_switch_to_url = false;
            ESP_LOGI(TAG, "Switching to WiFi QR code");
            bsp_display_lock(0);
            do_qr_transition(false);
            bsp_display_unlock();
        }
    }
}

// Generate unique SSID from MAC address
static void generate_unique_ssid(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    
    // Create SSID like "Voxels-A1B2C3" using last 3 bytes of MAC
    snprintf(wifi_ap_ssid, sizeof(wifi_ap_ssid), "%s%02X%02X%02X", 
             WIFI_AP_SSID_PREFIX, mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Generated unique SSID: %s", wifi_ap_ssid);
}

// Initialize WiFi in AP mode
static void wifi_init_softap(void)
{
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
    memcpy(wifi_config.ap.ssid, wifi_ap_ssid, strlen(wifi_ap_ssid));
    wifi_config.ap.ssid_len = strlen(wifi_ap_ssid);
    
    // If password is empty, use open authentication
    if (strlen(WIFI_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi AP started. SSID: %s, Password: %s", wifi_ap_ssid, WIFI_AP_PASS);
    ESP_LOGI(TAG, "Connect to this network and visit http://%s", AP_IP_ADDR);
}

// Forward declarations
static void create_main_ui(void);
static void show_format_dialog(void);
static void perform_format_and_continue(void);

// Format dialog button event handler
static void format_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        format_confirmed = true;
        ESP_LOGI(TAG, "Format button clicked");
    }
}

static void cancel_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        format_cancelled = true;
        ESP_LOGI(TAG, "Cancel button clicked");
    }
}

// Create format confirmation dialog
static void create_format_dialog(void)
{
    lv_obj_t *scr = lv_screen_active();
    
    // Set dark background
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    
    // Create dialog container
    format_dialog = lv_obj_create(scr);
    lv_obj_set_size(format_dialog, 380, 280);
    lv_obj_center(format_dialog);
    lv_obj_set_style_bg_color(format_dialog, lv_color_hex(0x252545), 0);
    lv_obj_set_style_border_color(format_dialog, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_border_width(format_dialog, 2, 0);
    lv_obj_set_style_radius(format_dialog, 20, 0);
    lv_obj_set_style_shadow_width(format_dialog, 30, 0);
    lv_obj_set_style_shadow_color(format_dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(format_dialog, LV_OPA_50, 0);
    lv_obj_set_flex_flow(format_dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(format_dialog, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(format_dialog, 25, 0);
    
    // Warning icon (using text)
    lv_obj_t *icon = lv_label_create(format_dialog);
    lv_label_set_text(icon, LV_SYMBOL_SD_CARD);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xffa500), 0);
    
    // Title
    lv_obj_t *title = lv_label_create(format_dialog);
    lv_label_set_text(title, "SD Card Setup Required");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_margin_top(title, 10, 0);
    
    // Message
    lv_obj_t *msg = lv_label_create(format_dialog);
    lv_label_set_text(msg, "SD card detected but not initialized.\nFormat the card to setup database?");
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_margin_top(msg, 10, 0);
    
    // Warning text
    lv_obj_t *warning = lv_label_create(format_dialog);
    lv_label_set_text(warning, "Warning: All data on card will be erased!");
    lv_obj_set_style_text_font(warning, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(warning, lv_color_hex(0xff6b6b), 0);
    lv_obj_set_style_margin_top(warning, 5, 0);
    
    // Button container
    lv_obj_t *btn_cont = lv_obj_create(format_dialog);
    lv_obj_set_size(btn_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_margin_top(btn_cont, 20, 0);
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(cancel_btn, 130, 45);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x3a3a5e), 0);
    lv_obj_set_style_radius(cancel_btn, 10, 0);
    lv_obj_add_event_cb(cancel_btn, cancel_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_16, 0);
    lv_obj_center(cancel_label);
    
    // Format button
    lv_obj_t *format_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(format_btn, 130, 45);
    lv_obj_set_style_bg_color(format_btn, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_radius(format_btn, 10, 0);
    lv_obj_add_event_cb(format_btn, format_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *format_label = lv_label_create(format_btn);
    lv_label_set_text(format_label, "Format");
    lv_obj_set_style_text_font(format_label, &lv_font_montserrat_16, 0);
    lv_obj_center(format_label);
}

// Timer callback to check for dialog response
static void format_dialog_check_cb(lv_timer_t *timer)
{
    if (format_confirmed) {
        ESP_LOGI(TAG, "User confirmed format");
        lv_timer_delete(timer);
        
        // Show formatting message
        if (format_dialog) {
            lv_obj_clean(format_dialog);
            
            lv_obj_t *msg = lv_label_create(format_dialog);
            lv_label_set_text(msg, "Formatting SD card...");
            lv_obj_set_style_text_font(msg, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(msg, lv_color_hex(0xffffff), 0);
            lv_obj_center(msg);
        }
        
        // Schedule format operation (needs to happen outside LVGL timer for display update)
        lv_timer_t *fmt_timer = lv_timer_create((lv_timer_cb_t)perform_format_and_continue, 100, NULL);
        lv_timer_set_repeat_count(fmt_timer, 1);
        
    } else if (format_cancelled) {
        ESP_LOGI(TAG, "User cancelled format");
        lv_timer_delete(timer);
        
        // Delete dialog and show QR code
        if (format_dialog) {
            lv_obj_delete(format_dialog);
            format_dialog = NULL;
        }
        
        ESP_LOGW(TAG, "Continuing without SD card database");
        create_main_ui();
    }
}

// Perform format operation and continue to main UI
static void perform_format_and_continue(void)
{
    // Perform format
    sd_db_status_t status = sd_db_format_and_init();
    
    // Delete dialog
    if (format_dialog) {
        lv_obj_delete(format_dialog);
        format_dialog = NULL;
    }
    
    if (status == SD_DB_READY) {
        ESP_LOGI(TAG, "SD card formatted and database initialized");
    } else {
        ESP_LOGE(TAG, "Failed to format SD card");
    }
    
    // Show QR code UI
    create_main_ui();
}

// Show format dialog (called after splash screen if SD needs setup)
static void show_format_dialog(void)
{
    format_confirmed = false;
    format_cancelled = false;
    
    create_format_dialog();
    
    // Timer will check for user response
    lv_timer_create(format_dialog_check_cb, 50, NULL);
}

// Actually update the QR code content (called after fade out)
static void update_qr_content(bool to_url)
{
    ESP_LOGI(TAG, "update_qr_content called: to_url=%d, info_label=%p, qr_code=%p", 
             to_url, (void*)info_label, (void*)qr_code);
    
    if (qr_code == NULL || info_label == NULL) {
        ESP_LOGE(TAG, "NULL pointer! qr_code=%p, info_label=%p", (void*)qr_code, (void*)info_label);
        return;
    }
    
    if (to_url) {
        ESP_LOGI(TAG, "Setting URL QR and 'Scan to Open Page' text");
        
        // Update QR code with URL
        char url[64];
        snprintf(url, sizeof(url), "http://%s", AP_IP_ADDR);
        lv_qrcode_update(qr_code, url, strlen(url));
        
        // Update labels
        lv_label_set_text(info_label, "Scan to Open Page");
        lv_obj_set_style_text_color(info_label, lv_color_hex(0x4caf50), 0);
        lv_obj_invalidate(info_label);  // Force redraw
        
        if (subtitle_label) {
            lv_label_set_text_fmt(subtitle_label, "Visit: http://%s", AP_IP_ADDR);
            lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x4caf50), 0);
            lv_obj_invalidate(subtitle_label);
        }
    } else {
        ESP_LOGI(TAG, "Setting WiFi QR and 'Scan to Connect WiFi' text");
        
        // WiFi QR code format: WIFI:T:WPA;S:<SSID>;P:<PASSWORD>;;
        char wifi_qr[128];
        snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:WPA;S:%s;P:%s;;", wifi_ap_ssid, WIFI_AP_PASS);
        lv_qrcode_update(qr_code, wifi_qr, strlen(wifi_qr));
        
        // Update labels
        lv_label_set_text(info_label, "Scan to Connect WiFi");
        lv_obj_set_style_text_color(info_label, lv_color_hex(0xe94560), 0);
        lv_obj_invalidate(info_label);  // Force redraw
        
        if (subtitle_label) {
            lv_label_set_text_fmt(subtitle_label, "Network: %s", wifi_ap_ssid);
            lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x888888), 0);
            lv_obj_invalidate(subtitle_label);
        }
    }
    
    ESP_LOGI(TAG, "update_qr_content completed");
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

// Called after splash fade to decide what to show next
static void after_splash_transition(void)
{
    // Delete splash screen
    if (splash_screen) {
        lv_obj_delete(splash_screen);
        splash_screen = NULL;
    }
    
    // Check if SD card needs initialization
    if (saved_db_status == SD_DB_NOT_INITIALIZED) {
        ESP_LOGI(TAG, "Showing SD card format dialog");
        show_format_dialog();
    } else {
        // Go directly to QR code UI
        create_main_ui();
    }
}

// Loading animation timer callback
static void loading_timer_cb(lv_timer_t *timer)
{
    loading_progress += 2;
    
    if (loading_progress <= 100) {
        lv_bar_set_value(loading_bar, loading_progress, LV_ANIM_ON);
    }
    
    if (loading_progress >= 100) {
        // Stop timer
        lv_timer_delete(timer);
        loading_timer = NULL;
        
        // Fade out splash screen
        lv_obj_fade_out(splash_screen, 300, 0);
        
        // Transition to next screen after fade (format dialog or QR code)
        lv_timer_t *ui_timer = lv_timer_create((lv_timer_cb_t)after_splash_transition, 350, NULL);
        lv_timer_set_repeat_count(ui_timer, 1);
    }
}

// Create splash/loading screen
static void create_splash_screen(void)
{
    // Set main screen background color first (so it shows when splash fades)
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x1a1a2e), 0);
    
    // Create splash screen
    splash_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(splash_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(splash_screen, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(splash_screen, 0, 0);
    lv_obj_set_style_radius(splash_screen, 0, 0);
    lv_obj_set_style_pad_all(splash_screen, 0, 0);
    lv_obj_center(splash_screen);
    
    // Create container for vertical layout
    lv_obj_t *container = lv_obj_create(splash_screen);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // VOXELS title
    lv_obj_t *title = lv_label_create(container);
    lv_label_set_text(title, "VOXELS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_letter_space(title, 8, 0);
    lv_obj_set_style_margin_bottom(title, 60, 0);
    
    // Loading bar container (for rounded background)
    lv_obj_t *bar_bg = lv_obj_create(container);
    lv_obj_set_size(bar_bg, 280, 24);
    lv_obj_set_style_bg_color(bar_bg, lv_color_hex(0x2a2a4e), 0);
    lv_obj_set_style_radius(bar_bg, 12, 0);
    lv_obj_set_style_border_width(bar_bg, 0, 0);
    lv_obj_set_style_pad_all(bar_bg, 4, 0);
    
    // Loading bar
    loading_bar = lv_bar_create(bar_bg);
    lv_obj_set_size(loading_bar, 272, 16);
    lv_obj_center(loading_bar);
    lv_bar_set_range(loading_bar, 0, 100);
    lv_bar_set_value(loading_bar, 0, LV_ANIM_OFF);
    
    // Bar styling
    lv_obj_set_style_bg_color(loading_bar, lv_color_hex(0x3a3a5e), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(loading_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(loading_bar, 8, LV_PART_MAIN);
    
    // Indicator (filled part) styling - gradient effect
    lv_obj_set_style_bg_color(loading_bar, lv_color_hex(0xe94560), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(loading_bar, lv_color_hex(0xff6b6b), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(loading_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_radius(loading_bar, 8, LV_PART_INDICATOR);
    
    // Loading text
    lv_obj_t *loading_text = lv_label_create(container);
    lv_label_set_text(loading_text, "Initializing...");
    lv_obj_set_style_text_font(loading_text, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(loading_text, lv_color_hex(0x666666), 0);
    lv_obj_set_style_margin_top(loading_text, 20, 0);
    
    // Start loading animation timer (updates every 30ms)
    loading_progress = 0;
    loading_timer = lv_timer_create(loading_timer_cb, 30, NULL);
}

// Create the main UI with QR code
static void create_main_ui(void)
{
    // Delete splash screen if it exists
    if (splash_screen) {
        lv_obj_delete(splash_screen);
        splash_screen = NULL;
    }
    
    // Delete format dialog if it exists
    if (format_dialog) {
        lv_obj_delete(format_dialog);
        format_dialog = NULL;
    }
    
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
    // WiFi QR code format: WIFI:T:WPA;S:<SSID>;P:<PASSWORD>;;
    char wifi_qr[128];
    snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:WPA;S:%s;P:%s;;", wifi_ap_ssid, WIFI_AP_PASS);
    
    qr_code = lv_qrcode_create(qr_container);
    lv_qrcode_set_size(qr_code, 200);
    lv_qrcode_set_dark_color(qr_code, lv_color_hex(0x1a1a2e));
    lv_qrcode_set_light_color(qr_code, lv_color_hex(0xffffff));
    lv_qrcode_update(qr_code, wifi_qr, strlen(wifi_qr));
    lv_obj_center(qr_code);
    
    // Subtitle label (shows network name or URL)
    subtitle_label = lv_label_create(main_container);
    lv_label_set_text_fmt(subtitle_label, "Network: %s", wifi_ap_ssid);
    lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x888888), 0);
    
    // Password hint
    lv_obj_t *pass_label = lv_label_create(main_container);
    lv_label_set_text_fmt(pass_label, "Password: %s", WIFI_AP_PASS);
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
    
    // Initialize WiFi AP
    wifi_init_softap();
    
    // Start web server
    start_webserver();
    
    // Start display
    bsp_display_start();
    
    // Lock display for LVGL operations
    bsp_display_lock(0);
    
    // Show splash screen with loading animation
    // After splash completes, will check SD status and show format dialog if needed
    create_splash_screen();
    
    // Unlock display
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Application started successfully!");
    ESP_LOGI(TAG, "Connect to WiFi '%s' with password '%s'", wifi_ap_ssid, WIFI_AP_PASS);
    ESP_LOGI(TAG, "Then scan the QR code or visit http://%s", AP_IP_ADDR);
}
