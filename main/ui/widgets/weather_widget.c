#include "weather_widget.h"
#include "widget_common.h"
#include "core/widget_manager.h"
#include "core/weather_service.h"
#include "core/font_size.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "weather_widget";

static lv_obj_t *weather_container = NULL;
static lv_obj_t *temp_label = NULL;
static lv_obj_t *condition_label = NULL;
static lv_obj_t *details_label = NULL;
static lv_obj_t *error_label = NULL;
static lv_timer_t *weather_timer = NULL;
static uint32_t loading_start_time = 0;  // Track when loading started

static void weather_widget_init(void)
{
    ESP_LOGI(TAG, "Weather widget initialized");
}

static void weather_update_cb(lv_timer_t *timer);

static void weather_widget_show(void)
{
    if (weather_container) {
        return;
    }
    
    bsp_display_lock(0);
    
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, WIDGET_COLOR_BG, 0);
    
    weather_container = lv_obj_create(scr);
    lv_obj_set_size(weather_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(weather_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(weather_container, 0, 0);
    lv_obj_set_flex_flow(weather_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(weather_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(weather_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Temperature label (large)
    temp_label = lv_label_create(weather_container);
    lv_label_set_text(temp_label, "--°C");
    lv_obj_set_style_text_font(temp_label, font_size_get_huge(), 0);
    lv_obj_set_style_text_color(temp_label, WIDGET_COLOR_TEXT, 0);
    lv_obj_set_style_margin_bottom(temp_label, 20, 0);
    
    // Condition label (medium)
    condition_label = lv_label_create(weather_container);
    lv_label_set_text(condition_label, "Loading...");
    lv_obj_set_style_text_font(condition_label, font_size_get_medium(), 0);
    lv_obj_set_style_text_color(condition_label, WIDGET_COLOR_MUTED, 0);
    lv_obj_set_style_margin_bottom(condition_label, 30, 0);
    
    // Details label (normal) - humidity and wind
    details_label = lv_label_create(weather_container);
    lv_label_set_text(details_label, "");
    lv_obj_set_style_text_font(details_label, font_size_get_normal(), 0);
    lv_obj_set_style_text_color(details_label, WIDGET_COLOR_MUTED, 0);
    
    // Error label (hidden by default)
    error_label = lv_label_create(weather_container);
    lv_label_set_text(error_label, "Configure zip code in settings");
    lv_obj_set_style_text_font(error_label, font_size_get_normal(), 0);
    lv_obj_set_style_text_color(error_label, WIDGET_COLOR_MUTED, 0);
    lv_obj_add_flag(error_label, LV_OBJ_FLAG_HIDDEN);
    
    // Create update timer (check every 5 seconds for updates, refresh every 10 minutes)
    weather_timer = lv_timer_create(weather_update_cb, 5000, NULL);
    
    // Initial update
    weather_update_cb(NULL);
    
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Weather widget shown");
}

static void weather_widget_hide(void)
{
    bsp_display_lock(0);
    
    // Stop timer first
    if (weather_timer) {
        lv_timer_delete(weather_timer);
        weather_timer = NULL;
    }
    
    // Reset loading timer
    loading_start_time = 0;
    
    // Clear pointers before deletion
    lv_obj_t *container_to_delete = weather_container;
    weather_container = NULL;
    temp_label = NULL;
    condition_label = NULL;
    details_label = NULL;
    error_label = NULL;
    
    // Delete container after clearing pointers
    if (container_to_delete) {
        lv_obj_delete(container_to_delete);
    }
    
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Weather widget hidden");
}

static void weather_update_cb(lv_timer_t *timer)
{
    (void)timer;
    
    // Check if widget is still active
    if (!weather_container) {
        return;
    }
    
    bsp_display_lock(0);
    
    // Double-check after acquiring lock
    if (!weather_container) {
        bsp_display_unlock();
        return;
    }
    
    weather_data_t weather = {0};
    esp_err_t ret = ESP_FAIL;
    
    // Try cached data first
    ret = weather_service_get_cached(&weather);
    
    // If no cached data, request a fetch (non-blocking, will update cache in background)
    if (ret != ESP_OK || !weather.valid) {
        char zip_code[16] = {0};
        if (weather_service_get_zip_code(zip_code, sizeof(zip_code)) == ESP_OK && strlen(zip_code) > 0) {
            // Zip code is configured, show "Loading..." while fetching
            
            // Track loading start time if not already tracking
            if (loading_start_time == 0) {
                loading_start_time = (uint32_t)time(NULL);
            }
            
            // Check if loading timeout exceeded (30 seconds)
            uint32_t now = (uint32_t)time(NULL);
            bool timeout_exceeded = (now - loading_start_time) > 30;
            
            weather_service_fetch(&weather);  // This queues a fetch request and returns immediately
            
            // Try to get cached data again (might have been updated by previous fetch)
            ret = weather_service_get_cached(&weather);
            
            if (ret == ESP_OK && weather.valid) {
                // Data is now available, reset loading timer
                loading_start_time = 0;
                // Fall through to display data
            } else if (timeout_exceeded) {
                // Show error after timeout
                loading_start_time = 0;  // Reset timer
                if (error_label) {
                    lv_obj_clear_flag(error_label, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text(error_label, "Failed to fetch weather");
                }
                if (temp_label) {
                    lv_obj_add_flag(temp_label, LV_OBJ_FLAG_HIDDEN);
                }
                if (condition_label) {
                    lv_obj_add_flag(condition_label, LV_OBJ_FLAG_HIDDEN);
                }
                if (details_label) {
                    lv_obj_add_flag(details_label, LV_OBJ_FLAG_HIDDEN);
                }
                
                bsp_display_unlock();
                return;
            } else {
                // Still loading, show loading state
                if (error_label) {
                    lv_obj_add_flag(error_label, LV_OBJ_FLAG_HIDDEN);
                }
                if (temp_label) {
                    lv_obj_add_flag(temp_label, LV_OBJ_FLAG_HIDDEN);
                }
                if (condition_label) {
                    lv_obj_clear_flag(condition_label, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text(condition_label, "Loading...");
                }
                if (details_label) {
                    lv_obj_add_flag(details_label, LV_OBJ_FLAG_HIDDEN);
                }
                
                bsp_display_unlock();
                return;  // Return early, will update when fetch completes
            }
        } else {
            // No zip code configured
            if (error_label) {
                lv_obj_clear_flag(error_label, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(error_label, "Configure zip code in settings");
            }
            if (temp_label) {
                lv_obj_add_flag(temp_label, LV_OBJ_FLAG_HIDDEN);
            }
            if (condition_label) {
                lv_obj_add_flag(condition_label, LV_OBJ_FLAG_HIDDEN);
            }
            if (details_label) {
                lv_obj_add_flag(details_label, LV_OBJ_FLAG_HIDDEN);
            }
            
            bsp_display_unlock();
            return;
        }
    }
    
    // We have valid cached data
    if (ret == ESP_OK && weather.valid) {
        // Reset loading timer since we have data
        loading_start_time = 0;
        
        // Hide error label, show data labels
        if (error_label) {
            lv_obj_add_flag(error_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (temp_label) {
            lv_obj_clear_flag(temp_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (condition_label) {
            lv_obj_clear_flag(condition_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (details_label) {
            lv_obj_clear_flag(details_label, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Update temperature (with correct unit symbol)
        if (temp_label) {
            char temp_str[32];
            weather_temp_unit_t unit = weather_service_get_temp_unit();
            const char *unit_symbol = (unit == WEATHER_TEMP_FAHRENHEIT) ? "°F" : "°C";
            snprintf(temp_str, sizeof(temp_str), "%.1f%s", weather.temperature, unit_symbol);
            lv_label_set_text(temp_label, temp_str);
        }
        
        // Update condition
        if (condition_label) {
            lv_label_set_text(condition_label, weather.condition);
        }
        
        // Update details (humidity and wind)
        if (details_label) {
            char details_str[64];
            snprintf(details_str, sizeof(details_str), "Humidity: %.0f%%\nWind: %.1f km/h", 
                    weather.humidity, weather.wind_speed);
            lv_label_set_text(details_label, details_str);
        }
    } else {
        // Should not reach here if we have valid cached data, but handle it anyway
        if (error_label) {
            lv_obj_clear_flag(error_label, LV_OBJ_FLAG_HIDDEN);
            char zip_code[16] = {0};
            if (weather_service_get_zip_code(zip_code, sizeof(zip_code)) == ESP_OK && strlen(zip_code) > 0) {
                lv_label_set_text(error_label, "Failed to fetch weather");
            } else {
                lv_label_set_text(error_label, "Configure zip code in settings");
            }
        }
        if (temp_label) {
            lv_obj_add_flag(temp_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (condition_label) {
            lv_obj_add_flag(condition_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (details_label) {
            lv_obj_add_flag(details_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    bsp_display_unlock();
}

static void weather_widget_update(void)
{
    weather_update_cb(NULL);
}

static cJSON* weather_widget_get_config(void)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "placeholder");
    return json;
}

static void weather_widget_set_config(cJSON *cfg)
{
    (void)cfg;
    // Placeholder
}

const struct widget weather_widget = {
    .id = "weather",
    .name = "Weather",
    .icon = "🌤️",
    .init = weather_widget_init,
    .show = weather_widget_show,
    .hide = weather_widget_hide,
    .update = weather_widget_update,
    .get_config = weather_widget_get_config,
    .set_config = weather_widget_set_config
};

