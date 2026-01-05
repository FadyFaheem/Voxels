#include "clock_widget.h"
#include "widget_common.h"
#include "core/widget_manager.h"
#include "core/time_sync.h"
#include "core/font_size.h"
#include "sd_database.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "cJSON.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "clock_widget";

typedef enum {
    CLOCK_MODE_DIGITAL,
    CLOCK_MODE_ANALOG
} clock_mode_t;

typedef struct {
    clock_mode_t mode;
    bool show_seconds;
    bool is_24h;
    bool show_date;
    bool show_weekday;
    bool smooth_seconds;
} clock_config_t;

static clock_config_t clock_config = {
    .mode = CLOCK_MODE_DIGITAL,
    .show_seconds = false,
    .is_24h = false,
    .show_date = true,
    .show_weekday = true,
    .smooth_seconds = true
};

// LVGL objects
static lv_obj_t *clock_container = NULL;
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *analog_face = NULL;
static lv_obj_t *hour_hand = NULL;
static lv_obj_t *minute_hand = NULL;
static lv_obj_t *second_hand = NULL;
static lv_timer_t *clock_timer = NULL;

// Forward declarations
static void clock_update_cb(lv_timer_t *timer);
static void create_digital_clock(void);
static void create_analog_clock(void);
static void update_digital_display(void);
static void update_analog_display(void);
static void load_config(void);
static void save_config(void);

static void clock_widget_init(void)
{
    load_config();
    ESP_LOGI(TAG, "Clock widget initialized");
}

static void clock_widget_show(void)
{
    if (clock_container) {
        return; // Already shown
    }
    
    bsp_display_lock(0);
    
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, WIDGET_COLOR_BG, 0);
    
    clock_container = lv_obj_create(scr);
    lv_obj_set_size(clock_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(clock_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_container, 0, 0);
    lv_obj_set_style_pad_all(clock_container, 20, 0);
    lv_obj_clear_flag(clock_container, LV_OBJ_FLAG_SCROLLABLE);
    
    if (clock_config.mode == CLOCK_MODE_DIGITAL) {
        create_digital_clock();
    } else {
        create_analog_clock();
    }
    
    // Create update timer
    int interval = (clock_config.mode == CLOCK_MODE_ANALOG || clock_config.show_seconds) ? 1000 : 60000;
    clock_timer = lv_timer_create(clock_update_cb, interval, NULL);
    
    // Initial update
    clock_update_cb(NULL);
    
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Clock widget shown");
}

static void clock_widget_hide(void)
{
    if (clock_timer) {
        lv_timer_delete(clock_timer);
        clock_timer = NULL;
    }
    
    if (clock_container) {
        lv_obj_delete(clock_container);
        clock_container = NULL;
    }
    
    time_label = NULL;
    date_label = NULL;
    analog_face = NULL;
    hour_hand = NULL;
    minute_hand = NULL;
    second_hand = NULL;
    
    ESP_LOGI(TAG, "Clock widget hidden");
}

static void clock_widget_update(void)
{
    clock_update_cb(NULL);
}

static void create_digital_clock(void)
{
    // Container for vertical layout
    lv_obj_t *container = lv_obj_create(clock_container);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Time label
    time_label = lv_label_create(container);
    lv_label_set_text(time_label, "00:00");
    lv_obj_set_style_text_font(time_label, font_size_get_huge(), 0);
    lv_obj_set_style_text_color(time_label, WIDGET_COLOR_TEXT, 0);
    lv_obj_set_style_margin_bottom(time_label, 20, 0);
    
    // Date label
    if (clock_config.show_date || clock_config.show_weekday) {
        date_label = lv_label_create(container);
        lv_label_set_text(date_label, "");
        lv_obj_set_style_text_font(date_label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(date_label, WIDGET_COLOR_MUTED, 0);
    }
}

static void create_analog_clock(void)
{
    // Analog clock face container
    analog_face = lv_obj_create(clock_container);
    lv_obj_set_size(analog_face, 360, 360);
    lv_obj_center(analog_face);
    lv_obj_set_style_bg_color(analog_face, lv_color_hex(0x2a2a4e), 0);
    lv_obj_set_style_bg_opa(analog_face, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(analog_face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(analog_face, 4, 0);
    lv_obj_set_style_border_color(analog_face, WIDGET_COLOR_TEXT, 0);
    lv_obj_clear_flag(analog_face, LV_OBJ_FLAG_SCROLLABLE);
    
    // Draw hour markers
    for (int i = 0; i < 12; i++) {
        lv_obj_t *marker = lv_obj_create(analog_face);
        lv_obj_set_size(marker, 4, 20);
        lv_obj_set_style_bg_color(marker, WIDGET_COLOR_TEXT, 0);
        lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(marker, 0, 0);
        lv_obj_set_style_radius(marker, 2, 0);
        
        float angle = (i * 30.0f - 90.0f) * M_PI / 180.0f;
        int x = 180 + (int)(150 * cosf(angle));
        int y = 180 + (int)(150 * sinf(angle));
        lv_obj_set_pos(marker, x - 2, y - 10);
    }
    
    // Hour hand
    hour_hand = lv_obj_create(analog_face);
    lv_obj_set_size(hour_hand, 6, 80);
    lv_obj_set_style_bg_color(hour_hand, WIDGET_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(hour_hand, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hour_hand, 0, 0);
    lv_obj_set_style_radius(hour_hand, 3, 0);
    lv_obj_set_pos(hour_hand, 177, 100);
    lv_obj_set_style_transform_pivot_x(hour_hand, 3, 0);
    lv_obj_set_style_transform_pivot_y(hour_hand, 80, 0);
    
    // Minute hand
    minute_hand = lv_obj_create(analog_face);
    lv_obj_set_size(minute_hand, 4, 120);
    lv_obj_set_style_bg_color(minute_hand, WIDGET_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(minute_hand, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(minute_hand, 0, 0);
    lv_obj_set_style_radius(minute_hand, 2, 0);
    lv_obj_set_pos(minute_hand, 178, 60);
    lv_obj_set_style_transform_pivot_x(minute_hand, 2, 0);
    lv_obj_set_style_transform_pivot_y(minute_hand, 120, 0);
    
    // Second hand
    if (clock_config.show_seconds) {
        second_hand = lv_obj_create(analog_face);
        lv_obj_set_size(second_hand, 2, 130);
        lv_obj_set_style_bg_color(second_hand, WIDGET_COLOR_ACCENT, 0);
        lv_obj_set_style_bg_opa(second_hand, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(second_hand, 0, 0);
        lv_obj_set_style_radius(second_hand, 1, 0);
        lv_obj_set_pos(second_hand, 179, 50);
        lv_obj_set_style_transform_pivot_x(second_hand, 1, 0);
        lv_obj_set_style_transform_pivot_y(second_hand, 130, 0);
    }
    
    // Center dot
    lv_obj_t *center = lv_obj_create(analog_face);
    lv_obj_set_size(center, 12, 12);
    lv_obj_set_style_bg_color(center, WIDGET_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(center, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(center);
    
    // Date label below clock
    if (clock_config.show_date || clock_config.show_weekday) {
        date_label = lv_label_create(clock_container);
        lv_label_set_text(date_label, "");
        lv_obj_set_style_text_font(date_label, font_size_get_medium(), 0);
        lv_obj_set_style_text_color(date_label, WIDGET_COLOR_MUTED, 0);
        lv_obj_align(date_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    }
}

static void update_digital_display(void)
{
    if (!time_label) return;
    
    time_t now;
    struct tm timeinfo;
    
    if (time_sync_get_time(&now) != ESP_OK) {
        lv_label_set_text(time_label, "--:--");
        return;
    }
    
    localtime_r(&now, &timeinfo);
    
    char time_str[32];
    char ampm_str[4];
    if (clock_config.show_seconds) {
        if (clock_config.is_24h) {
            strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
        } else {
            strftime(time_str, sizeof(time_str), "%I:%M:%S", &timeinfo);
            strftime(ampm_str, sizeof(ampm_str), "%p", &timeinfo);
            // Remove leading zero from hour
            if (time_str[0] == '0') {
                memmove(time_str, time_str + 1, strlen(time_str));
            }
            // Append AM/PM
            strncat(time_str, " ", sizeof(time_str) - strlen(time_str) - 1);
            strncat(time_str, ampm_str, sizeof(time_str) - strlen(time_str) - 1);
        }
    } else {
        if (clock_config.is_24h) {
            strftime(time_str, sizeof(time_str), "%H:%M", &timeinfo);
        } else {
            strftime(time_str, sizeof(time_str), "%I:%M", &timeinfo);
            strftime(ampm_str, sizeof(ampm_str), "%p", &timeinfo);
            if (time_str[0] == '0') {
                memmove(time_str, time_str + 1, strlen(time_str));
            }
            // Append AM/PM
            strncat(time_str, " ", sizeof(time_str) - strlen(time_str) - 1);
            strncat(time_str, ampm_str, sizeof(time_str) - strlen(time_str) - 1);
        }
    }
    
    lv_label_set_text(time_label, time_str);
    
    // Update date label
    if (date_label && (clock_config.show_date || clock_config.show_weekday)) {
        char date_str[64];
        if (clock_config.show_weekday && clock_config.show_date) {
            strftime(date_str, sizeof(date_str), "%A, %b %d", &timeinfo);
        } else if (clock_config.show_weekday) {
            strftime(date_str, sizeof(date_str), "%A", &timeinfo);
        } else {
            strftime(date_str, sizeof(date_str), "%b %d", &timeinfo);
        }
        lv_label_set_text(date_label, date_str);
    }
}

static void update_analog_display(void)
{
    if (!analog_face) return;
    
    time_t now;
    struct tm timeinfo;
    
    if (time_sync_get_time(&now) != ESP_OK) {
        return;
    }
    
    localtime_r(&now, &timeinfo);
    
    // Calculate angles (0 degrees = 12 o'clock, clockwise)
    float hour_angle = ((timeinfo.tm_hour % 12) * 30.0f + timeinfo.tm_min * 0.5f - 90.0f) * M_PI / 180.0f;
    float minute_angle = (timeinfo.tm_min * 6.0f - 90.0f) * M_PI / 180.0f;
    float second_angle = (timeinfo.tm_sec * 6.0f - 90.0f) * M_PI / 180.0f;
    
    // Update hour hand
    if (hour_hand) {
        lv_obj_set_style_transform_angle(hour_hand, (int32_t)(hour_angle * 1800.0f / M_PI), 0);
    }
    
    // Update minute hand
    if (minute_hand) {
        lv_obj_set_style_transform_angle(minute_hand, (int32_t)(minute_angle * 1800.0f / M_PI), 0);
    }
    
    // Update second hand
    if (second_hand && clock_config.show_seconds) {
        lv_obj_set_style_transform_angle(second_hand, (int32_t)(second_angle * 1800.0f / M_PI), 0);
    }
    
    // Update date label
    if (date_label && (clock_config.show_date || clock_config.show_weekday)) {
        char date_str[64];
        if (clock_config.show_weekday && clock_config.show_date) {
            strftime(date_str, sizeof(date_str), "%A, %b %d", &timeinfo);
        } else if (clock_config.show_weekday) {
            strftime(date_str, sizeof(date_str), "%A", &timeinfo);
        } else {
            strftime(date_str, sizeof(date_str), "%b %d", &timeinfo);
        }
        lv_label_set_text(date_label, date_str);
    }
}

static void clock_update_cb(lv_timer_t *timer)
{
    (void)timer;
    
    bsp_display_lock(0);
    
    if (clock_config.mode == CLOCK_MODE_DIGITAL) {
        update_digital_display();
    } else {
        update_analog_display();
    }
    
    bsp_display_unlock();
}

static void load_config(void)
{
    if (!sd_db_is_ready()) {
        return;
    }
    
    char config_json[512];
    if (sd_db_get_string("widget_clock_config", config_json, sizeof(config_json)) != ESP_OK) {
        return;
    }
    
    cJSON *json = cJSON_Parse(config_json);
    if (!json) {
        return;
    }
    
    cJSON *item;
    item = cJSON_GetObjectItem(json, "mode");
    if (item && cJSON_IsString(item)) {
        clock_config.mode = (strcmp(item->valuestring, "analog") == 0) ? CLOCK_MODE_ANALOG : CLOCK_MODE_DIGITAL;
    }
    
    item = cJSON_GetObjectItem(json, "show_seconds");
    if (item && cJSON_IsBool(item)) {
        clock_config.show_seconds = cJSON_IsTrue(item);
    }
    
    item = cJSON_GetObjectItem(json, "is_24h");
    if (item && cJSON_IsBool(item)) {
        clock_config.is_24h = cJSON_IsTrue(item);
    }
    
    item = cJSON_GetObjectItem(json, "show_date");
    if (item && cJSON_IsBool(item)) {
        clock_config.show_date = cJSON_IsTrue(item);
    }
    
    item = cJSON_GetObjectItem(json, "show_weekday");
    if (item && cJSON_IsBool(item)) {
        clock_config.show_weekday = cJSON_IsTrue(item);
    }
    
    item = cJSON_GetObjectItem(json, "smooth_seconds");
    if (item && cJSON_IsBool(item)) {
        clock_config.smooth_seconds = cJSON_IsTrue(item);
    }
    
    cJSON_Delete(json);
    ESP_LOGI(TAG, "Clock config loaded");
}

static void save_config(void)
{
    if (!sd_db_is_ready()) {
        return;
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "mode", clock_config.mode == CLOCK_MODE_ANALOG ? "analog" : "digital");
    cJSON_AddBoolToObject(json, "show_seconds", clock_config.show_seconds);
    cJSON_AddBoolToObject(json, "is_24h", clock_config.is_24h);
    cJSON_AddBoolToObject(json, "show_date", clock_config.show_date);
    cJSON_AddBoolToObject(json, "show_weekday", clock_config.show_weekday);
    cJSON_AddBoolToObject(json, "smooth_seconds", clock_config.smooth_seconds);
    
    char *json_str = cJSON_PrintUnformatted(json);
    if (json_str) {
        sd_db_set_string("widget_clock_config", json_str);
        sd_db_save();
        free(json_str);
    }
    
    cJSON_Delete(json);
}

static cJSON* clock_widget_get_config(void)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "mode", clock_config.mode == CLOCK_MODE_ANALOG ? "analog" : "digital");
    cJSON_AddBoolToObject(json, "show_seconds", clock_config.show_seconds);
    cJSON_AddBoolToObject(json, "is_24h", clock_config.is_24h);
    cJSON_AddBoolToObject(json, "show_date", clock_config.show_date);
    cJSON_AddBoolToObject(json, "show_weekday", clock_config.show_weekday);
    cJSON_AddBoolToObject(json, "smooth_seconds", clock_config.smooth_seconds);
    return json;
}

static void clock_widget_set_config(cJSON *cfg)
{
    if (!cfg) return;
    
    cJSON *item;
    item = cJSON_GetObjectItem(cfg, "mode");
    if (item && cJSON_IsString(item)) {
        clock_config.mode = (strcmp(item->valuestring, "analog") == 0) ? CLOCK_MODE_ANALOG : CLOCK_MODE_DIGITAL;
    }
    
    item = cJSON_GetObjectItem(cfg, "show_seconds");
    if (item && cJSON_IsBool(item)) {
        clock_config.show_seconds = cJSON_IsTrue(item);
    }
    
    item = cJSON_GetObjectItem(cfg, "is_24h");
    if (item && cJSON_IsBool(item)) {
        clock_config.is_24h = cJSON_IsTrue(item);
    }
    
    item = cJSON_GetObjectItem(cfg, "show_date");
    if (item && cJSON_IsBool(item)) {
        clock_config.show_date = cJSON_IsTrue(item);
    }
    
    item = cJSON_GetObjectItem(cfg, "show_weekday");
    if (item && cJSON_IsBool(item)) {
        clock_config.show_weekday = cJSON_IsTrue(item);
    }
    
    item = cJSON_GetObjectItem(cfg, "smooth_seconds");
    if (item && cJSON_IsBool(item)) {
        clock_config.smooth_seconds = cJSON_IsTrue(item);
    }
    
    save_config();
    
    // If widget is currently shown, recreate it with new config
    if (clock_container) {
        clock_widget_hide();
        clock_widget_show();
    }
}

// Widget structure
const struct widget clock_widget = {
    .id = "clock",
    .name = "Clock",
    .icon = "🕐",
    .init = clock_widget_init,
    .show = clock_widget_show,
    .hide = clock_widget_hide,
    .update = clock_widget_update,
    .get_config = clock_widget_get_config,
    .set_config = clock_widget_set_config
};

