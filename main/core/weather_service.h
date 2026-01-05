#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Weather service using Open-Meteo API
 * https://open-meteo.com/
 */

/**
 * @brief Temperature unit enumeration
 */
typedef enum {
    WEATHER_TEMP_CELSIUS = 0,
    WEATHER_TEMP_FAHRENHEIT = 1
} weather_temp_unit_t;

/**
 * @brief Weather data structure
 */
typedef struct {
    float temperature;        // Current temperature (in selected unit)
    float humidity;           // Relative humidity %
    float wind_speed;        // Wind speed in km/h
    int weather_code;         // WMO weather code
    char condition[32];       // Human-readable condition (e.g., "Sunny", "Cloudy")
    bool valid;               // True if data is valid
    uint32_t timestamp;       // Unix timestamp of when data was fetched
} weather_data_t;

/**
 * @brief Initialize weather service
 */
void weather_service_init(void);

/**
 * @brief Set zip code for weather location
 * @param zip_code Zip code string (e.g., "90210" or "10001")
 * @return ESP_OK on success
 */
esp_err_t weather_service_set_zip_code(const char *zip_code);

/**
 * @brief Get current zip code
 * @param zip_code Buffer to store zip code
 * @param max_len Maximum length of buffer
 * @return ESP_OK on success
 */
esp_err_t weather_service_get_zip_code(char *zip_code, size_t max_len);

/**
 * @brief Fetch weather data from Open-Meteo API
 * @param data Pointer to weather_data_t structure to fill
 * @return ESP_OK on success
 */
esp_err_t weather_service_fetch(weather_data_t *data);

/**
 * @brief Get cached weather data (if available and recent)
 * @param data Pointer to weather_data_t structure to fill
 * @return ESP_OK if cached data is available and recent, ESP_FAIL otherwise
 */
esp_err_t weather_service_get_cached(weather_data_t *data);

/**
 * @brief Set temperature unit preference
 * @param unit Temperature unit (WEATHER_TEMP_CELSIUS or WEATHER_TEMP_FAHRENHEIT)
 * @return ESP_OK on success
 */
esp_err_t weather_service_set_temp_unit(weather_temp_unit_t unit);

/**
 * @brief Get current temperature unit preference
 * @return Current temperature unit
 */
weather_temp_unit_t weather_service_get_temp_unit(void);

#ifdef __cplusplus
}
#endif

