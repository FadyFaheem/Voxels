#include "calendar_widget.h"
#include "widget_common.h"
#include "core/widget_manager.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "cJSON.h"

static const char *TAG = "calendar_widget";

static lv_obj_t *calendar_container = NULL;
static lv_obj_t *message_label = NULL;

static void calendar_widget_init(void)
{
    ESP_LOGI(TAG, "Calendar widget initialized");
}

static void calendar_widget_show(void)
{
    if (calendar_container) {
        return;
    }
    
    bsp_display_lock(0);
    
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, WIDGET_COLOR_BG, 0);
    
    calendar_container = lv_obj_create(scr);
    lv_obj_set_size(calendar_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(calendar_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(calendar_container, 0, 0);
    lv_obj_set_flex_flow(calendar_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(calendar_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(calendar_container, LV_OBJ_FLAG_SCROLLABLE);
    
    message_label = lv_label_create(calendar_container);
    lv_label_set_text(message_label, "Calendar Widget\n\nConnect calendar in settings");
    lv_obj_set_style_text_font(message_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(message_label, WIDGET_COLOR_MUTED, 0);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Calendar widget shown");
}

static void calendar_widget_hide(void)
{
    if (calendar_container) {
        lv_obj_delete(calendar_container);
        calendar_container = NULL;
    }
    message_label = NULL;
    ESP_LOGI(TAG, "Calendar widget hidden");
}

static void calendar_widget_update(void)
{
    // Placeholder - no updates needed
}

static cJSON* calendar_widget_get_config(void)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "placeholder");
    return json;
}

static void calendar_widget_set_config(cJSON *cfg)
{
    (void)cfg;
    // Placeholder
}

const struct widget calendar_widget = {
    .id = "calendar",
    .name = "Calendar",
    .icon = "📅",
    .init = calendar_widget_init,
    .show = calendar_widget_show,
    .hide = calendar_widget_hide,
    .update = calendar_widget_update,
    .get_config = calendar_widget_get_config,
    .set_config = calendar_widget_set_config
};

