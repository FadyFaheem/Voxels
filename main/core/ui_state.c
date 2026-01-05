#include "ui_state.h"
#include "widget_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui_state";
static bool initialized = false;

void ui_state_init(void)
{
    initialized = true;
    ESP_LOGI(TAG, "UI state manager initialized");
}

const char* ui_state_get_active_widget(void)
{
    return widget_manager_get_active();
}

esp_err_t ui_state_notify_config_changed(const char *widget_id)
{
    if (!widget_id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *active = widget_manager_get_active();
    
    // If the changed widget is currently active, ensure it's refreshed
    if (active && strcmp(active, widget_id) == 0) {
        ESP_LOGI(TAG, "Config changed for active widget '%s', ensuring refresh", widget_id);
        // Widget's set_config should handle refresh, but we can force it if needed
        return widget_manager_refresh();
    }
    
    ESP_LOGI(TAG, "Config changed for inactive widget '%s'", widget_id);
    return ESP_OK;
}

esp_err_t ui_state_notify_widget_switched(const char *widget_id)
{
    if (!widget_id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Widget switched to: %s", widget_id);
    return ESP_OK;
}

esp_err_t ui_state_refresh(void)
{
    return widget_manager_refresh();
}

