#include "weather_service.h"
#include "sd_database.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "weather_service";

#define OPEN_METEO_GEOCODING_API "https://api.open-meteo.com/v1/search"
#define OPEN_METEO_FORECAST_API "https://api.open-meteo.com/v1/forecast"
#define WEATHER_CACHE_TIMEOUT_SEC 600  // 10 minutes

static char zip_code[16] = {0};
static weather_data_t cached_weather = {0};
static float cached_latitude = 0.0f;
static float cached_longitude = 0.0f;

// Structure to pass response buffer through event handler
typedef struct {
    char *buffer;
    int len;
    int max_len;
} http_response_t;

// HTTP event handler for geocoding
static esp_err_t geocoding_http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *response = (http_response_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client) && response) {
                int len = evt->data_len;
                if (response->buffer && (response->len + len) < response->max_len) {
                    memcpy(response->buffer + response->len, evt->data, len);
                    response->len += len;
                    response->buffer[response->len] = '\0';
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Geocode zip code to latitude/longitude using Open-Meteo Geocoding API
static esp_err_t geocode_zip_code(const char *zip, float *latitude, float *longitude)
{
    if (!zip || strlen(zip) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char url[256];
    // URL encode the zip code (simple version - just handle spaces)
    char encoded_zip[32];
    int j = 0;
    for (int i = 0; zip[i] && j < sizeof(encoded_zip) - 1; i++) {
        if (zip[i] == ' ') {
            encoded_zip[j++] = '+';
        } else {
            encoded_zip[j++] = zip[i];
        }
    }
    encoded_zip[j] = '\0';
    
    snprintf(url, sizeof(url), "%s?name=%s&count=1&language=en&format=json", 
             OPEN_METEO_GEOCODING_API, encoded_zip);

    ESP_LOGI(TAG, "Geocoding zip code: %s", zip);

    char response_buffer[2048] = {0};
    http_response_t response = {
        .buffer = response_buffer,
        .len = 0,
        .max_len = sizeof(response_buffer) - 1
    };

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = geocoding_http_event_handler,
        .user_data = &response,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        
        ESP_LOGI(TAG, "HTTP Status = %d, response length = %d", status_code, response.len);
        
        if (status_code == 200 && response.len > 0) {
            // Parse JSON response
            cJSON *json = cJSON_Parse(response.buffer);
            if (json) {
                cJSON *results = cJSON_GetObjectItem(json, "results");
                if (results && cJSON_IsArray(results) && cJSON_GetArraySize(results) > 0) {
                    cJSON *first_result = cJSON_GetArrayItem(results, 0);
                    cJSON *lat = cJSON_GetObjectItem(first_result, "latitude");
                    cJSON *lon = cJSON_GetObjectItem(first_result, "longitude");
                    
                    if (lat && lon && cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
                        *latitude = (float)cJSON_GetNumberValue(lat);
                        *longitude = (float)cJSON_GetNumberValue(lon);
                        ESP_LOGI(TAG, "Geocoded %s to lat=%.4f, lon=%.4f", zip, *latitude, *longitude);
                        cJSON_Delete(json);
                        esp_http_client_cleanup(client);
                        return ESP_OK;
                    }
                }
                cJSON_Delete(json);
            }
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return ESP_FAIL;
}

// Convert WMO weather code to human-readable condition
static void weather_code_to_condition(int code, char *condition, size_t len)
{
    // WMO Weather interpretation codes (WW)
    // https://www.nodc.noaa.gov/archive/arc0021/0002199/1.1/data/0-data/HTML/WMO-CODE/WMO4677.HTM
    if (code == 0) {
        snprintf(condition, len, "Clear");
    } else if (code >= 1 && code <= 3) {
        snprintf(condition, len, "Cloudy");
    } else if (code >= 45 && code <= 48) {
        snprintf(condition, len, "Foggy");
    } else if (code >= 51 && code <= 67) {
        snprintf(condition, len, "Rainy");
    } else if (code >= 71 && code <= 77) {
        snprintf(condition, len, "Snowy");
    } else if (code >= 80 && code <= 82) {
        snprintf(condition, len, "Rain Showers");
    } else if (code >= 85 && code <= 86) {
        snprintf(condition, len, "Snow Showers");
    } else if (code >= 95 && code <= 99) {
        snprintf(condition, len, "Thunderstorm");
    } else {
        snprintf(condition, len, "Unknown");
    }
}

// HTTP event handler for weather data
static esp_err_t weather_http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *response = (http_response_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client) && response) {
                int len = evt->data_len;
                if (response->buffer && (response->len + len) < response->max_len) {
                    memcpy(response->buffer + response->len, evt->data, len);
                    response->len += len;
                    response->buffer[response->len] = '\0';
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Fetch weather data from Open-Meteo Forecast API
static esp_err_t fetch_weather_data(float latitude, float longitude, weather_data_t *data)
{
    char url[512];
    snprintf(url, sizeof(url), 
             "%s?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code&timezone=auto",
             OPEN_METEO_FORECAST_API, latitude, longitude);

    ESP_LOGI(TAG, "Fetching weather data");

    char response_buffer[4096] = {0};
    http_response_t response = {
        .buffer = response_buffer,
        .len = 0,
        .max_len = sizeof(response_buffer) - 1
    };

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = weather_http_event_handler,
        .user_data = &response,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        
        ESP_LOGI(TAG, "HTTP Status = %d, response length = %d", status_code, response.len);
        
        if (status_code == 200 && response.len > 0) {
            // Parse JSON response
            cJSON *json = cJSON_Parse(response.buffer);
            if (json) {
                cJSON *current = cJSON_GetObjectItem(json, "current");
                if (current) {
                    cJSON *temp = cJSON_GetObjectItem(current, "temperature_2m");
                    cJSON *humidity = cJSON_GetObjectItem(current, "relative_humidity_2m");
                    cJSON *wind = cJSON_GetObjectItem(current, "wind_speed_10m");
                    cJSON *weather_code = cJSON_GetObjectItem(current, "weather_code");
                    
                    if (temp && cJSON_IsNumber(temp)) {
                        data->temperature = (float)cJSON_GetNumberValue(temp);
                    }
                    if (humidity && cJSON_IsNumber(humidity)) {
                        data->humidity = (float)cJSON_GetNumberValue(humidity);
                    }
                    if (wind && cJSON_IsNumber(wind)) {
                        data->wind_speed = (float)cJSON_GetNumberValue(wind);
                    }
                    if (weather_code && cJSON_IsNumber(weather_code)) {
                        data->weather_code = (int)cJSON_GetNumberValue(weather_code);
                        weather_code_to_condition(data->weather_code, data->condition, sizeof(data->condition));
                    }
                    
                    data->valid = true;
                    data->timestamp = (uint32_t)time(NULL);
                    
                    ESP_LOGI(TAG, "Weather: %.1f°C, %.0f%% humidity, %.1f km/h wind, %s", 
                            data->temperature, data->humidity, data->wind_speed, data->condition);
                    
                    cJSON_Delete(json);
                    esp_http_client_cleanup(client);
                    return ESP_OK;
                }
                cJSON_Delete(json);
            }
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return ESP_FAIL;
}

static void load_zip_code(void)
{
    if (sd_db_is_ready()) {
        if (sd_db_get_string("weather_zip_code", zip_code, sizeof(zip_code)) == ESP_OK) {
            if (strlen(zip_code) > 0) {
                ESP_LOGI(TAG, "Loaded zip code from storage: %s", zip_code);
            }
        }
    }
}

static void save_zip_code(void)
{
    if (sd_db_is_ready()) {
        sd_db_set_string("weather_zip_code", zip_code);
        sd_db_save();
        ESP_LOGI(TAG, "Saved zip code to storage: %s", zip_code);
    }
}

void weather_service_init(void)
{
    load_zip_code();
    memset(&cached_weather, 0, sizeof(cached_weather));
    ESP_LOGI(TAG, "Weather service initialized");
}

esp_err_t weather_service_set_zip_code(const char *zip_code_str)
{
    if (!zip_code_str || strlen(zip_code_str) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(zip_code, zip_code_str, sizeof(zip_code) - 1);
    zip_code[sizeof(zip_code) - 1] = '\0';
    
    // Clear cached coordinates and weather data when zip code changes
    cached_latitude = 0.0f;
    cached_longitude = 0.0f;
    memset(&cached_weather, 0, sizeof(cached_weather));
    
    save_zip_code();
    ESP_LOGI(TAG, "Zip code set to: %s", zip_code);
    return ESP_OK;
}

esp_err_t weather_service_get_zip_code(char *zip_code_out, size_t max_len)
{
    if (!zip_code_out || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(zip_code_out, zip_code, max_len - 1);
    zip_code_out[max_len - 1] = '\0';
    return ESP_OK;
}

esp_err_t weather_service_fetch(weather_data_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strlen(zip_code) == 0) {
        ESP_LOGW(TAG, "No zip code configured");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Geocode zip code if we don't have coordinates cached
    if (cached_latitude == 0.0f && cached_longitude == 0.0f) {
        if (geocode_zip_code(zip_code, &cached_latitude, &cached_longitude) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to geocode zip code: %s", zip_code);
            return ESP_FAIL;
        }
    }
    
    // Fetch weather data
    if (fetch_weather_data(cached_latitude, cached_longitude, data) == ESP_OK) {
        // Cache the data
        memcpy(&cached_weather, data, sizeof(weather_data_t));
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

esp_err_t weather_service_get_cached(weather_data_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!cached_weather.valid) {
        return ESP_FAIL;
    }
    
    // Check if cache is still valid (within timeout)
    uint32_t now = (uint32_t)time(NULL);
    if (now - cached_weather.timestamp > WEATHER_CACHE_TIMEOUT_SEC) {
        ESP_LOGI(TAG, "Weather cache expired");
        return ESP_FAIL;
    }
    
    memcpy(data, &cached_weather, sizeof(weather_data_t));
    return ESP_OK;
}

