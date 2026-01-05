#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UI State Manager
 * Tracks the current UI state and ensures widgets are properly refreshed
 */

/**
 * @brief Initialize the UI state manager
 */
void ui_state_init(void);

/**
 * @brief Get the currently active widget ID
 * @return Widget ID string, or NULL if none active
 */
const char* ui_state_get_active_widget(void);

/**
 * @brief Notify that a widget config has changed
 * @param widget_id Widget ID that changed
 * @return ESP_OK on success
 */
esp_err_t ui_state_notify_config_changed(const char *widget_id);

/**
 * @brief Notify that a widget was switched
 * @param widget_id Widget ID that was switched to
 * @return ESP_OK on success
 */
esp_err_t ui_state_notify_widget_switched(const char *widget_id);

/**
 * @brief Force refresh of the current UI state
 * @return ESP_OK on success
 */
esp_err_t ui_state_refresh(void);

#ifdef __cplusplus
}
#endif

