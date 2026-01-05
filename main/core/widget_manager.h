#pragma once

#include "esp_err.h"
#include "cJSON.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Widget interface structure
 * All widgets must implement these functions
 */
typedef struct widget widget_t;

struct widget {
    const char *id;              // Unique widget ID (e.g., "clock", "timer")
    const char *name;            // Display name (e.g., "Clock", "Timer")
    const char *icon;            // Icon identifier for web UI
    
    // Lifecycle functions
    void (*init)(void);          // One-time initialization
    void (*show)(void);          // Create LVGL objects, start timers
    void (*hide)(void);          // Cleanup LVGL objects, stop timers
    void (*update)(void);        // Periodic refresh (called by timer)
    
    // Configuration functions
    cJSON* (*get_config)(void);  // Get current config as JSON
    void (*set_config)(cJSON *cfg);  // Apply config from JSON
};

/**
 * @brief Initialize the widget manager
 * Must be called before registering widgets
 */
void widget_manager_init(void);

/**
 * @brief Register a widget with the manager
 * @param widget Widget structure (must persist for lifetime of app)
 */
void widget_manager_register(const widget_t *widget);

/**
 * @brief Switch to a different widget
 * @param widget_id Widget ID to switch to
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if widget doesn't exist
 */
esp_err_t widget_manager_switch(const char *widget_id);

/**
 * @brief Get the currently active widget ID
 * @return Widget ID string, or NULL if none active
 */
const char* widget_manager_get_active(void);

/**
 * @brief Get list of all registered widgets as JSON array
 * @return JSON array with widget metadata, caller must free with cJSON_Delete
 */
cJSON* widget_manager_list_widgets(void);

/**
 * @brief Get configuration for a specific widget
 * @param widget_id Widget ID
 * @return JSON object with config, caller must free with cJSON_Delete
 */
cJSON* widget_manager_get_config(const char *widget_id);

/**
 * @brief Set configuration for a specific widget
 * @param widget_id Widget ID
 * @param cfg JSON object with config
 * @return ESP_OK on success
 */
esp_err_t widget_manager_set_config(const char *widget_id, cJSON *cfg);

/**
 * @brief Check if a widget is registered
 * @param widget_id Widget ID to check
 * @return true if widget exists
 */
bool widget_manager_widget_exists(const char *widget_id);

/**
 * @brief Refresh the currently active widget
 * Forces a hide/show cycle to update the display
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if no widget is active
 */
esp_err_t widget_manager_refresh(void);

#ifdef __cplusplus
}
#endif

