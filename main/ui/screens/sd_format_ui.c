#include "sd_format_ui.h"
#include "esp_log.h"

static const char *TAG = "sd_format_ui";

// Format dialog objects
static lv_obj_t *format_dialog = NULL;
static volatile bool format_confirmed = false;
static volatile bool format_cancelled = false;

// Callback for when dialog completes
static sd_format_complete_cb_t complete_callback = NULL;

// Forward declarations
static void create_format_dialog(void);
static void format_dialog_check_cb(lv_timer_t *timer);
static void perform_format_and_continue(void);

// Button event handlers
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

void sd_format_ui_init(sd_format_complete_cb_t on_complete)
{
    complete_callback = on_complete;
    format_dialog = NULL;
    format_confirmed = false;
    format_cancelled = false;
}

void sd_format_ui_show(void)
{
    format_confirmed = false;
    format_cancelled = false;
    
    create_format_dialog();
    
    // Timer will check for user response
    lv_timer_create(format_dialog_check_cb, 50, NULL);
}

bool sd_format_ui_is_active(void)
{
    return format_dialog != NULL;
}

void sd_format_ui_cleanup(void)
{
    if (format_dialog) {
        lv_obj_delete(format_dialog);
        format_dialog = NULL;
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
    lv_obj_clear_flag(format_dialog, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling
    lv_obj_set_style_bg_color(format_dialog, lv_color_hex(0x252545), 0);
    lv_obj_set_style_border_color(format_dialog, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_border_width(format_dialog, 2, 0);
    lv_obj_set_style_radius(format_dialog, 20, 0);
    lv_obj_set_style_shadow_width(format_dialog, 30, 0);
    lv_obj_set_style_shadow_color(format_dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(format_dialog, LV_OPA_50, 0);
    lv_obj_set_flex_flow(format_dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(format_dialog, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(format_dialog, 20, 0);
    
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
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_margin_top(btn_cont, 15, 0);
    
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
        
        // Delete dialog
        sd_format_ui_cleanup();
        
        ESP_LOGW(TAG, "Continuing without SD card database");
        
        // Call completion callback
        if (complete_callback) {
            complete_callback();
        }
    }
}

// Perform format operation and continue to main UI
static void perform_format_and_continue(void)
{
    // Perform format
    sd_db_status_t status = sd_db_format_and_init();
    
    // Delete dialog
    sd_format_ui_cleanup();
    
    if (status == SD_DB_READY) {
        ESP_LOGI(TAG, "SD card formatted and database initialized");
    } else {
        ESP_LOGE(TAG, "Failed to format SD card");
    }
    
    // Call completion callback
    if (complete_callback) {
        complete_callback();
    }
}

