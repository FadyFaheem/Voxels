#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback type for when splash screen completes
 */
typedef void (*splash_complete_cb_t)(void);

/**
 * @brief Initialize the splash UI module
 * @param on_complete Callback function to call when splash animation finishes
 */
void splash_ui_init(splash_complete_cb_t on_complete);

/**
 * @brief Show the splash screen with loading animation
 */
void splash_ui_show(void);

/**
 * @brief Check if splash screen is currently shown
 * @return true if splash is visible
 */
bool splash_ui_is_active(void);

/**
 * @brief Delete the splash screen if it exists
 */
void splash_ui_cleanup(void);

#ifdef __cplusplus
}
#endif

