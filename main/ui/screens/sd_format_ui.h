#pragma once

#include "lvgl.h"
#include "sd_database.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback type for when format dialog completes
 */
typedef void (*sd_format_complete_cb_t)(void);

/**
 * @brief Initialize the SD format UI module
 * @param on_complete Callback function to call when dialog completes (format or cancel)
 */
void sd_format_ui_init(sd_format_complete_cb_t on_complete);

/**
 * @brief Show the SD card format confirmation dialog
 * Call this when sd_db_init() returns SD_DB_NOT_INITIALIZED
 */
void sd_format_ui_show(void);

/**
 * @brief Check if the format dialog is currently shown
 * @return true if dialog is visible
 */
bool sd_format_ui_is_active(void);

/**
 * @brief Delete the format dialog if it exists
 */
void sd_format_ui_cleanup(void);

#ifdef __cplusplus
}
#endif

