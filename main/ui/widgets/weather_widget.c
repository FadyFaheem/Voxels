#include "weather_widget.h"
#include "widget_common.h"
#include "core/widget_manager.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "cJSON.h"

static const char *TAG = "weather_widget";

static lv_obj_t *weather_container = NULL;
static lv_obj_t *message_label = NULL;

static void weather_widget_init(void)
{
    ESP_LOGI(TAG, "Weather widget initialized");
}

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
    
    message_label = lv_label_create(weather_container);
    lv_label_set_text(message_label, "Weather Widget\n\nConfigure API in settings");
    lv_obj_set_style_text_font(message_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(message_label, WIDGET_COLOR_MUTED, 0);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Weather widget shown");
}

static void weather_widget_hide(void)
{
    if (weather_container) {
        lv_obj_delete(weather_container);
        weather_container = NULL;
    }
    message_label = NULL;
    ESP_LOGI(TAG, "Weather widget hidden");
}

static void weather_widget_update(void)
{
    // Placeholder - no updates needed
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

