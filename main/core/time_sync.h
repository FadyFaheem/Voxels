#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SNTP time synchronization
 * Should be called after WiFi is connected
 */
void time_sync_init(void);

/**
 * @brief Check if time is synchronized
 * @return true if time is synced
 */
bool time_sync_is_synced(void);

/**
 * @brief Get current time
 * @param now Pointer to time_t to fill
 * @return ESP_OK on success
 */
esp_err_t time_sync_get_time(time_t *now);

/**
 * @brief Set timezone
 * @param tz Timezone string (e.g., "EST5EDT" or "PST8PDT")
 * @return ESP_OK on success
 */
esp_err_t time_sync_set_timezone(const char *tz);

/**
 * @brief Get timezone
 * @return Timezone string, or NULL if not set
 */
const char* time_sync_get_timezone(void);

#ifdef __cplusplus
}
#endif

