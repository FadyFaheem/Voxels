#include "timer_widget.h"
#include "widget_common.h"
#include "core/widget_manager.h"
#include "core/font_size.h"
#include "sd_database.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "cJSON.h"

static const char *TAG = "timer_widget";

typedef enum {
    TIMER_MODE_COUNTDOWN,
    TIMER_MODE_STOPWATCH
} timer_mode_t;

typedef struct {
    timer_mode_t mode;
    int duration_seconds;  // For countdown
    int elapsed_seconds;   // For stopwatch
    bool running;
    bool paused;
} timer_config_t;

static timer_config_t timer_config = {
    .mode = TIMER_MODE_COUNTDOWN,
    .duration_seconds = 300,  // 5 minutes default
    .elapsed_seconds = 0,
    .running = false,
    .paused = false
};

static lv_obj_t *timer_container = NULL;
static lv_obj_t *time_label = NULL;
static lv_obj_t *mode_label = NULL;
static lv_obj_t *progress_bar = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *start_pause_btn = NULL;
static lv_obj_t *reset_btn = NULL;
static lv_obj_t *time_adjust_container = NULL;  // For countdown time adjustment
static lv_timer_t *timer_timer = NULL;

static void timer_update_cb(lv_timer_t *timer);
static void load_config(void);
static void save_config(void);
static void format_time(int seconds, char *buf, size_t len);
static void start_pause_btn_event_cb(lv_event_t *e);
static void reset_btn_event_cb(lv_event_t *e);
static void time_adjust_btn_event_cb(lv_event_t *e);
static void update_control_buttons(void);

static void timer_widget_init(void)
{
    load_config();
    ESP_LOGI(TAG, "Timer widget initialized");
}

static void timer_widget_show(void)
{
    if (timer_container) {
        return;
    }
    
    bsp_display_lock(0);
    
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, WIDGET_COLOR_BG, 0);
    
    timer_container = lv_obj_create(scr);
    lv_obj_set_size(timer_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(timer_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(timer_container, 0, 0);
    lv_obj_set_flex_flow(timer_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(timer_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(timer_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Mode label
    mode_label = lv_label_create(timer_container);
    lv_label_set_text(mode_label, timer_config.mode == TIMER_MODE_COUNTDOWN ? "Countdown Timer" : "Stopwatch");
    lv_obj_set_style_text_font(mode_label, font_size_get_medium(), 0);
    lv_obj_set_style_text_color(mode_label, WIDGET_COLOR_MUTED, 0);
    lv_obj_set_style_margin_bottom(mode_label, 20, 0);
    
    // Time display
    time_label = lv_label_create(timer_container);
    char time_str[32];
    if (timer_config.mode == TIMER_MODE_COUNTDOWN) {
        format_time(timer_config.duration_seconds, time_str, sizeof(time_str));
    } else {
        format_time(timer_config.elapsed_seconds, time_str, sizeof(time_str));
    }
    lv_label_set_text(time_label, time_str);
    lv_obj_set_style_text_font(time_label, font_size_get_huge(), 0);
    lv_obj_set_style_text_color(time_label, WIDGET_COLOR_TEXT, 0);
    lv_obj_set_style_margin_bottom(time_label, 30, 0);
    
    // Progress bar (for countdown)
    if (timer_config.mode == TIMER_MODE_COUNTDOWN) {
        progress_bar = lv_bar_create(timer_container);
        lv_obj_set_size(progress_bar, 300, 20);
        lv_bar_set_range(progress_bar, 0, timer_config.duration_seconds);
        lv_bar_set_value(progress_bar, timer_config.duration_seconds, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x2a2a4e), LV_PART_MAIN);
        lv_obj_set_style_bg_color(progress_bar, WIDGET_COLOR_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_margin_bottom(progress_bar, 20, 0);
    }
    
    // Status label
    status_label = lv_label_create(timer_container);
    lv_label_set_text(status_label, timer_config.running ? "Running" : (timer_config.paused ? "Paused" : "Stopped"));
    lv_obj_set_style_text_font(status_label, font_size_get_normal(), 0);
    lv_obj_set_style_text_color(status_label, WIDGET_COLOR_MUTED, 0);
    lv_obj_set_style_margin_top(status_label, 20, 0);
    lv_obj_set_style_margin_bottom(status_label, 30, 0);
    
    // Control buttons container
    lv_obj_t *btn_container = lv_obj_create(timer_container);
    lv_obj_set_size(btn_container, 400, 80);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_container, 0, 0);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_container, 0, 0);
    lv_obj_set_style_gap(btn_container, 20, 0);
    lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Start/Pause button
    start_pause_btn = lv_btn_create(btn_container);
    lv_obj_set_size(start_pause_btn, 150, 60);
    lv_obj_set_style_bg_color(start_pause_btn, WIDGET_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(start_pause_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(start_pause_btn, 12, 0);
    lv_obj_add_event_cb(start_pause_btn, start_pause_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *start_pause_label = lv_label_create(start_pause_btn);
    lv_label_set_text(start_pause_label, timer_config.running ? "Pause" : "Start");
    lv_obj_set_style_text_font(start_pause_label, font_size_get_large(), 0);
    lv_obj_set_style_text_color(start_pause_label, lv_color_white(), 0);
    lv_obj_center(start_pause_label);
    
    // Reset button
    reset_btn = lv_btn_create(btn_container);
    lv_obj_set_size(reset_btn, 150, 60);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_bg_opa(reset_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(reset_btn, 12, 0);
    lv_obj_add_event_cb(reset_btn, reset_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "Reset");
    lv_obj_set_style_text_font(reset_label, font_size_get_large(), 0);
    lv_obj_set_style_text_color(reset_label, lv_color_white(), 0);
    lv_obj_center(reset_label);
    
    // Time adjustment buttons (for countdown mode when stopped)
    if (timer_config.mode == TIMER_MODE_COUNTDOWN) {
        time_adjust_container = lv_obj_create(timer_container);
        lv_obj_set_size(time_adjust_container, 400, 60);
        lv_obj_set_style_bg_opa(time_adjust_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(time_adjust_container, 0, 0);
        lv_obj_set_flex_flow(time_adjust_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(time_adjust_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(time_adjust_container, 0, 0);
        lv_obj_set_style_gap(time_adjust_container, 15, 0);
        lv_obj_set_style_margin_top(time_adjust_container, 20, 0);
        lv_obj_clear_flag(time_adjust_container, LV_OBJ_FLAG_SCROLLABLE);
        
        // -1 min button
        lv_obj_t *btn_minus1 = lv_btn_create(time_adjust_container);
        lv_obj_set_size(btn_minus1, 80, 50);
        lv_obj_set_style_bg_color(btn_minus1, lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(btn_minus1, 8, 0);
        lv_obj_add_event_cb(btn_minus1, time_adjust_btn_event_cb, LV_EVENT_CLICKED, (void*)-60);
        lv_obj_t *label_minus1 = lv_label_create(btn_minus1);
        lv_label_set_text(label_minus1, "-1m");
        lv_obj_set_style_text_font(label_minus1, font_size_get_normal(), 0);
        lv_obj_center(label_minus1);
        
        // -10 sec button
        lv_obj_t *btn_minus10 = lv_btn_create(time_adjust_container);
        lv_obj_set_size(btn_minus10, 80, 50);
        lv_obj_set_style_bg_color(btn_minus10, lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(btn_minus10, 8, 0);
        lv_obj_add_event_cb(btn_minus10, time_adjust_btn_event_cb, LV_EVENT_CLICKED, (void*)-10);
        lv_obj_t *label_minus10 = lv_label_create(btn_minus10);
        lv_label_set_text(label_minus10, "-10s");
        lv_obj_set_style_text_font(label_minus10, font_size_get_normal(), 0);
        lv_obj_center(label_minus10);
        
        // +10 sec button
        lv_obj_t *btn_plus10 = lv_btn_create(time_adjust_container);
        lv_obj_set_size(btn_plus10, 80, 50);
        lv_obj_set_style_bg_color(btn_plus10, lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(btn_plus10, 8, 0);
        lv_obj_add_event_cb(btn_plus10, time_adjust_btn_event_cb, LV_EVENT_CLICKED, (void*)10);
        lv_obj_t *label_plus10 = lv_label_create(btn_plus10);
        lv_label_set_text(label_plus10, "+10s");
        lv_obj_set_style_text_font(label_plus10, font_size_get_normal(), 0);
        lv_obj_center(label_plus10);
        
        // +1 min button
        lv_obj_t *btn_plus1 = lv_btn_create(time_adjust_container);
        lv_obj_set_size(btn_plus1, 80, 50);
        lv_obj_set_style_bg_color(btn_plus1, lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(btn_plus1, 8, 0);
        lv_obj_add_event_cb(btn_plus1, time_adjust_btn_event_cb, LV_EVENT_CLICKED, (void*)60);
        lv_obj_t *label_plus1 = lv_label_create(btn_plus1);
        lv_label_set_text(label_plus1, "+1m");
        lv_obj_set_style_text_font(label_plus1, font_size_get_normal(), 0);
        lv_obj_center(label_plus1);
        
        // Show/hide adjustment buttons based on timer state
        if (timer_config.running || timer_config.paused) {
            lv_obj_add_flag(time_adjust_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    update_control_buttons();
    
    // Create update timer (1 second interval)
    timer_timer = lv_timer_create(timer_update_cb, 1000, NULL);
    
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Timer widget shown");
}

static void timer_widget_hide(void)
{
    if (timer_timer) {
        lv_timer_delete(timer_timer);
        timer_timer = NULL;
    }
    
    if (timer_container) {
        lv_obj_delete(timer_container);
        timer_container = NULL;
    }
    
    time_label = NULL;
    mode_label = NULL;
    progress_bar = NULL;
    status_label = NULL;
    start_pause_btn = NULL;
    reset_btn = NULL;
    time_adjust_container = NULL;
    
    ESP_LOGI(TAG, "Timer widget hidden");
}

static void timer_widget_update(void)
{
    timer_update_cb(NULL);
}

static void format_time(int seconds, char *buf, size_t len)
{
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;
    
    if (hours > 0) {
        snprintf(buf, len, "%02d:%02d:%02d", hours, minutes, secs);
    } else {
        snprintf(buf, len, "%02d:%02d", minutes, secs);
    }
}

static void timer_update_cb(lv_timer_t *timer)
{
    (void)timer;
    
    if (!timer_config.running) {
        return;
    }
    
    bsp_display_lock(0);
    
    if (timer_config.mode == TIMER_MODE_COUNTDOWN) {
        timer_config.duration_seconds--;
        if (timer_config.duration_seconds < 0) {
            timer_config.duration_seconds = 0;
            timer_config.running = false;
            timer_config.paused = false;
            // Update buttons when timer completes
            update_control_buttons();
            // TODO: Add alert/notification
        }
        
        char time_str[32];
        format_time(timer_config.duration_seconds, time_str, sizeof(time_str));
        if (time_label) {
            lv_label_set_text(time_label, time_str);
        }
        
        if (progress_bar) {
            lv_bar_set_value(progress_bar, timer_config.duration_seconds, LV_ANIM_ON);
        }
    } else {
        timer_config.elapsed_seconds++;
        char time_str[32];
        format_time(timer_config.elapsed_seconds, time_str, sizeof(time_str));
        if (time_label) {
            lv_label_set_text(time_label, time_str);
        }
    }
    
    bsp_display_unlock();
}

static void load_config(void)
{
    if (!sd_db_is_ready()) {
        return;
    }
    
    char config_json[512];
    if (sd_db_get_string("widget_timer_config", config_json, sizeof(config_json)) != ESP_OK) {
        return;
    }
    
    cJSON *json = cJSON_Parse(config_json);
    if (!json) {
        return;
    }
    
    cJSON *item;
    item = cJSON_GetObjectItem(json, "mode");
    if (item && cJSON_IsString(item)) {
        timer_config.mode = (strcmp(item->valuestring, "stopwatch") == 0) ? TIMER_MODE_STOPWATCH : TIMER_MODE_COUNTDOWN;
    }
    
    item = cJSON_GetObjectItem(json, "duration_seconds");
    if (item && cJSON_IsNumber(item)) {
        timer_config.duration_seconds = item->valueint;
    }
    
    cJSON_Delete(json);
    ESP_LOGI(TAG, "Timer config loaded");
}

static void save_config(void)
{
    if (!sd_db_is_ready()) {
        return;
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "mode", timer_config.mode == TIMER_MODE_STOPWATCH ? "stopwatch" : "countdown");
    cJSON_AddNumberToObject(json, "duration_seconds", timer_config.duration_seconds);
    
    char *json_str = cJSON_PrintUnformatted(json);
    if (json_str) {
        sd_db_set_string("widget_timer_config", json_str);
        sd_db_save();
        free(json_str);
    }
    
    cJSON_Delete(json);
}

static cJSON* timer_widget_get_config(void)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "mode", timer_config.mode == TIMER_MODE_STOPWATCH ? "stopwatch" : "countdown");
    cJSON_AddNumberToObject(json, "duration_seconds", timer_config.duration_seconds);
    cJSON_AddBoolToObject(json, "running", timer_config.running);
    cJSON_AddBoolToObject(json, "paused", timer_config.paused);
    return json;
}

static void update_control_buttons(void)
{
    if (!start_pause_btn || !status_label) return;
    
    bsp_display_lock(0);
    
    // Update start/pause button text
    lv_obj_t *btn_label = lv_obj_get_child(start_pause_btn, 0);
    if (btn_label) {
        if (timer_config.running) {
            lv_label_set_text(btn_label, "Pause");
        } else if (timer_config.paused) {
            lv_label_set_text(btn_label, "Resume");
        } else {
            lv_label_set_text(btn_label, "Start");
        }
    }
    
    // Update status label
    if (status_label) {
        lv_label_set_text(status_label, timer_config.running ? "Running" : (timer_config.paused ? "Paused" : "Stopped"));
    }
    
    // Show/hide time adjustment buttons (only for countdown when stopped)
    if (time_adjust_container) {
        if (timer_config.running || timer_config.paused) {
            lv_obj_add_flag(time_adjust_container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(time_adjust_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    bsp_display_unlock();
}

static void start_pause_btn_event_cb(lv_event_t *e)
{
    (void)e;
    
    ESP_LOGI(TAG, "Start/Pause button pressed");
    
    bsp_display_lock(0);
    
    if (timer_config.running) {
        // Pause
        timer_config.running = false;
        timer_config.paused = true;
        ESP_LOGI(TAG, "Timer paused");
    } else {
        // Start or Resume
        timer_config.running = true;
        timer_config.paused = false;
        
        // For countdown, ensure we don't go below 0
        if (timer_config.mode == TIMER_MODE_COUNTDOWN && timer_config.duration_seconds <= 0) {
            timer_config.duration_seconds = 300; // Reset to 5 minutes if at 0
        }
        
        ESP_LOGI(TAG, "Timer %s", timer_config.paused ? "resumed" : "started");
    }
    
    save_config();
    update_control_buttons();
    
    bsp_display_unlock();
}

static void reset_btn_event_cb(lv_event_t *e)
{
    (void)e;
    
    ESP_LOGI(TAG, "Reset button pressed");
    
    bsp_display_lock(0);
    
    timer_config.running = false;
    timer_config.paused = false;
    
    if (timer_config.mode == TIMER_MODE_COUNTDOWN) {
        // Reset to saved duration (or default)
        timer_config.duration_seconds = 300; // Default 5 minutes
        if (time_label) {
            char time_str[32];
            format_time(timer_config.duration_seconds, time_str, sizeof(time_str));
            lv_label_set_text(time_label, time_str);
        }
        if (progress_bar) {
            lv_bar_set_range(progress_bar, 0, timer_config.duration_seconds);
            lv_bar_set_value(progress_bar, timer_config.duration_seconds, LV_ANIM_OFF);
        }
    } else {
        // Stopwatch: reset to 0
        timer_config.elapsed_seconds = 0;
        if (time_label) {
            char time_str[32];
            format_time(0, time_str, sizeof(time_str));
            lv_label_set_text(time_label, time_str);
        }
    }
    
    save_config();
    update_control_buttons();
    
    bsp_display_unlock();
}

static void time_adjust_btn_event_cb(lv_event_t *e)
{
    int32_t seconds = (int32_t)(intptr_t)lv_event_get_user_data(e);
    
    if (timer_config.mode != TIMER_MODE_COUNTDOWN || timer_config.running || timer_config.paused) {
        return; // Only allow adjustment when stopped
    }
    
    ESP_LOGI(TAG, "Time adjustment: %d seconds", seconds);
    
    bsp_display_lock(0);
    
    timer_config.duration_seconds += seconds;
    
    // Clamp to reasonable values (0 to 99:59:59)
    if (timer_config.duration_seconds < 0) {
        timer_config.duration_seconds = 0;
    } else if (timer_config.duration_seconds > 359999) {
        timer_config.duration_seconds = 359999; // 99:59:59
    }
    
    // Update display
    if (time_label) {
        char time_str[32];
        format_time(timer_config.duration_seconds, time_str, sizeof(time_str));
        lv_label_set_text(time_label, time_str);
    }
    
    if (progress_bar) {
        lv_bar_set_range(progress_bar, 0, timer_config.duration_seconds);
        lv_bar_set_value(progress_bar, timer_config.duration_seconds, LV_ANIM_OFF);
    }
    
    save_config();
    
    bsp_display_unlock();
}

static void timer_widget_set_config(cJSON *cfg)
{
    if (!cfg) return;
    
    cJSON *item;
    item = cJSON_GetObjectItem(cfg, "mode");
    if (item && cJSON_IsString(item)) {
        timer_config.mode = (strcmp(item->valuestring, "stopwatch") == 0) ? TIMER_MODE_STOPWATCH : TIMER_MODE_COUNTDOWN;
    }
    
    item = cJSON_GetObjectItem(cfg, "duration_seconds");
    if (item && cJSON_IsNumber(item)) {
        timer_config.duration_seconds = item->valueint;
    }
    
    item = cJSON_GetObjectItem(cfg, "running");
    if (item && cJSON_IsBool(item)) {
        timer_config.running = cJSON_IsTrue(item);
    }
    
    item = cJSON_GetObjectItem(cfg, "paused");
    if (item && cJSON_IsBool(item)) {
        timer_config.paused = cJSON_IsTrue(item);
    }
    
    save_config();
    
    // If widget is currently shown, recreate it with new config
    if (timer_container) {
        timer_widget_hide();
        timer_widget_show();
    }
}

const struct widget timer_widget = {
    .id = "timer",
    .name = "Timer",
    .icon = "⏱️",
    .init = timer_widget_init,
    .show = timer_widget_show,
    .hide = timer_widget_hide,
    .update = timer_widget_update,
    .get_config = timer_widget_get_config,
    .set_config = timer_widget_set_config
};

