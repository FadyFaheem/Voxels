#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Database initialization status
 */
typedef enum {
    SD_DB_NOT_PRESENT,      // No SD card detected
    SD_DB_NOT_INITIALIZED,  // SD card present but no database
    SD_DB_READY,            // Database ready to use
    SD_DB_ERROR             // Error occurred
} sd_db_status_t;

/**
 * @brief Initialize the SD card database
 * 
 * Mounts SD card and checks for existing database.
 * Returns SD_DB_NOT_INITIALIZED if database marker is missing - caller should
 * prompt user for formatting and call sd_db_format_and_init() if confirmed.
 * 
 * @return SD_DB_READY on success, SD_DB_NOT_INITIALIZED if needs format, other status on failure
 */
sd_db_status_t sd_db_init(void);

/**
 * @brief Format SD card and initialize fresh database
 * 
 * Call this after user confirms formatting when sd_db_init returns SD_DB_NOT_INITIALIZED.
 * 
 * @return SD_DB_READY on success, SD_DB_ERROR on failure
 */
sd_db_status_t sd_db_format_and_init(void);

/**
 * @brief Check if database is ready
 * @return true if database is mounted and ready
 */
bool sd_db_is_ready(void);

/**
 * @brief Get the current database status
 * @return Current status
 */
sd_db_status_t sd_db_get_status(void);

/**
 * @brief Get the storage type being used
 * @return "SD Card", "NVS Flash", or "None"
 */
const char* sd_db_get_storage_type(void);

/**
 * @brief Wipe and reinitialize the SD card
 * @return ESP_OK on success
 */
esp_err_t sd_db_wipe(void);

/**
 * @brief Set a string value in the database
 * @param key Key name (max 64 chars)
 * @param value Value to store
 * @return ESP_OK on success
 */
esp_err_t sd_db_set_string(const char *key, const char *value);

/**
 * @brief Get a string value from the database
 * @param key Key name
 * @param value Buffer to store the value
 * @param max_len Maximum length of buffer
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if key doesn't exist
 */
esp_err_t sd_db_get_string(const char *key, char *value, size_t max_len);

/**
 * @brief Set an integer value in the database
 * @param key Key name
 * @param value Value to store
 * @return ESP_OK on success
 */
esp_err_t sd_db_set_int(const char *key, int value);

/**
 * @brief Get an integer value from the database
 * @param key Key name
 * @param value Pointer to store the value
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if key doesn't exist
 */
esp_err_t sd_db_get_int(const char *key, int *value);

/**
 * @brief Delete a key from the database
 * @param key Key name
 * @return ESP_OK on success
 */
esp_err_t sd_db_delete(const char *key);

/**
 * @brief Check if a key exists in the database
 * @param key Key name
 * @return true if key exists
 */
bool sd_db_key_exists(const char *key);

/**
 * @brief Save all pending changes to SD card
 * @return ESP_OK on success
 */
esp_err_t sd_db_save(void);

/**
 * @brief Unmount the SD card database
 * @return ESP_OK on success
 */
esp_err_t sd_db_deinit(void);

#ifdef __cplusplus
}
#endif

