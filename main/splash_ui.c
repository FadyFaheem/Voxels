#include "splash_ui.h"
#include "esp_log.h"

static const char *TAG = "splash_ui";

// Splash screen objects
static lv_obj_t *splash_screen = NULL;
static lv_obj_t *loading_bar = NULL;
static lv_timer_t *loading_timer = NULL;
static int loading_progress = 0;

// Callback for when splash completes
static splash_complete_cb_t complete_callback = NULL;

// Forward declarations
static void loading_timer_cb(lv_timer_t *timer);
static void after_splash_fade(void);

void splash_ui_init(splash_complete_cb_t on_complete)
{
    complete_callback = on_complete;
    splash_screen = NULL;
    loading_bar = NULL;
    loading_timer = NULL;
    loading_progress = 0;
}

void splash_ui_show(void)
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
    
    ESP_LOGI(TAG, "Splash screen shown");
}

bool splash_ui_is_active(void)
{
    return splash_screen != NULL;
}

void splash_ui_cleanup(void)
{
    if (loading_timer) {
        lv_timer_delete(loading_timer);
        loading_timer = NULL;
    }
    
    if (splash_screen) {
        lv_obj_delete(splash_screen);
        splash_screen = NULL;
    }
    
    loading_bar = NULL;
    loading_progress = 0;
}

// Called after splash fade completes
static void after_splash_fade(void)
{
    // Delete splash screen
    splash_ui_cleanup();
    
    ESP_LOGI(TAG, "Splash complete, calling callback");
    
    // Call completion callback
    if (complete_callback) {
        complete_callback();
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
        
        // Transition after fade
        lv_timer_t *fade_timer = lv_timer_create((lv_timer_cb_t)after_splash_fade, 350, NULL);
        lv_timer_set_repeat_count(fade_timer, 1);
    }
}

