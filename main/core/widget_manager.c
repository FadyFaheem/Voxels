#include "widget_manager.h"
#include "ui_state.h"
#include "sd_database.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "widget_manager";

#define MAX_WIDGETS 16

static const widget_t *registered_widgets[MAX_WIDGETS];
static int widget_count = 0;
static const widget_t *active_widget = NULL;

void widget_manager_init(void)
{
    widget_count = 0;
    active_widget = NULL;
    memset(registered_widgets, 0, sizeof(registered_widgets));
    
    ESP_LOGI(TAG, "Widget manager initialized");
}

void widget_manager_register(const widget_t *widget)
{
    if (widget_count >= MAX_WIDGETS) {
        ESP_LOGE(TAG, "Too many widgets registered (max %d)", MAX_WIDGETS);
        return;
    }
    
    if (!widget || !widget->id || !widget->name) {
        ESP_LOGE(TAG, "Invalid widget structure");
        return;
    }
    
    // Check for duplicate IDs
    for (int i = 0; i < widget_count; i++) {
        if (strcmp(registered_widgets[i]->id, widget->id) == 0) {
            ESP_LOGW(TAG, "Widget '%s' already registered, skipping", widget->id);
            return;
        }
    }
    
    registered_widgets[widget_count++] = widget;
    ESP_LOGI(TAG, "Registered widget: %s (%s)", widget->id, widget->name);
    
    // Call widget's init function if provided
    if (widget->init) {
        widget->init();
    }
}

esp_err_t widget_manager_switch(const char *widget_id)
{
    if (!widget_id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find widget
    const widget_t *new_widget = NULL;
    for (int i = 0; i < widget_count; i++) {
        if (strcmp(registered_widgets[i]->id, widget_id) == 0) {
            new_widget = registered_widgets[i];
            break;
        }
    }
    
    if (!new_widget) {
        ESP_LOGE(TAG, "Widget '%s' not found", widget_id);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Hide current widget
    if (active_widget && active_widget->hide) {
        active_widget->hide();
    }
    
    // Show new widget
    if (new_widget->show) {
        new_widget->show();
    }
    
    active_widget = new_widget;
    
    // Persist to database (survives reboot)
    if (sd_db_is_ready()) {
        sd_db_set_string("active_widget", widget_id);
        sd_db_save();
        ESP_LOGI(TAG, "Saved active widget '%s' to database", widget_id);
    }
    
    // Notify UI state manager
    ui_state_notify_widget_switched(widget_id);
    
    ESP_LOGI(TAG, "Switched to widget: %s", widget_id);
    return ESP_OK;
}

const char* widget_manager_get_active(void)
{
    return active_widget ? active_widget->id : NULL;
}

cJSON* widget_manager_list_widgets(void)
{
    cJSON *array = cJSON_CreateArray();
    
    for (int i = 0; i < widget_count; i++) {
        const widget_t *w = registered_widgets[i];
        cJSON *widget_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(widget_obj, "id", w->id);
        cJSON_AddStringToObject(widget_obj, "name", w->name);
        if (w->icon) {
            cJSON_AddStringToObject(widget_obj, "icon", w->icon);
        }
        cJSON_AddBoolToObject(widget_obj, "active", (active_widget == w));
        cJSON_AddItemToArray(array, widget_obj);
    }
    
    return array;
}

cJSON* widget_manager_get_config(const char *widget_id)
{
    if (!widget_id) {
        return NULL;
    }
    
    // Find widget
    const widget_t *widget = NULL;
    for (int i = 0; i < widget_count; i++) {
        if (strcmp(registered_widgets[i]->id, widget_id) == 0) {
            widget = registered_widgets[i];
            break;
        }
    }
    
    if (!widget || !widget->get_config) {
        return NULL;
    }
    
    return widget->get_config();
}

esp_err_t widget_manager_set_config(const char *widget_id, cJSON *cfg)
{
    if (!widget_id || !cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find widget
    const widget_t *widget = NULL;
    for (int i = 0; i < widget_count; i++) {
        if (strcmp(registered_widgets[i]->id, widget_id) == 0) {
            widget = registered_widgets[i];
            break;
        }
    }
    
    if (!widget || !widget->set_config) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Check if this widget is currently active
    bool is_active = (active_widget == widget);
    
    // Apply config (widget's set_config should handle refresh internally if widget is shown)
    // The clock widget and other widgets should refresh themselves in set_config
    widget->set_config(cfg);
    
    // Save config to database
    if (sd_db_is_ready()) {
        char *json_str = cJSON_PrintUnformatted(cfg);
        if (json_str) {
            char key[64];
            snprintf(key, sizeof(key), "widget_%s_config", widget_id);
            sd_db_set_string(key, json_str);
            sd_db_save();
            free(json_str);
        }
    }
    
    // Notify UI state manager of config change
    ui_state_notify_config_changed(widget_id);
    
    ESP_LOGI(TAG, "Config updated for widget: %s (active: %s)", widget_id, is_active ? "yes" : "no");
    return ESP_OK;
}

esp_err_t widget_manager_refresh(void)
{
    if (!active_widget) {
        ESP_LOGW(TAG, "No active widget to refresh");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Refreshing active widget: %s", active_widget->id);
    
    // Hide and show to force refresh
    if (active_widget->hide) {
        active_widget->hide();
    }
    
    if (active_widget->show) {
        active_widget->show();
    }
    
    ESP_LOGI(TAG, "Widget refreshed: %s", active_widget->id);
    return ESP_OK;
}

bool widget_manager_widget_exists(const char *widget_id)
{
    if (!widget_id) {
        return false;
    }
    
    for (int i = 0; i < widget_count; i++) {
        if (strcmp(registered_widgets[i]->id, widget_id) == 0) {
            return true;
        }
    }
    
    return false;
}

