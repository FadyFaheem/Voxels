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
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Forward declaration for certificate bundle attach function
// This is provided by mbedtls component when CONFIG_MBEDTLS_CERTIFICATE_BUNDLE is enabled
extern esp_err_t esp_crt_bundle_attach(void *conf);

static const char *TAG = "weather_service";

#define OPEN_METEO_GEOCODING_API "https://geocoding-api.open-meteo.com/v1/search"
#define OPEN_METEO_FORECAST_API "https://api.open-meteo.com/v1/forecast"
#define WEATHER_CACHE_TIMEOUT_SEC 600  // 10 minutes

static char zip_code[16] = {0};
static weather_data_t cached_weather = {0};
static float cached_latitude = 0.0f;
static float cached_longitude = 0.0f;
static weather_temp_unit_t temp_unit = WEATHER_TEMP_CELSIUS;  // Default to Celsius

// Task and synchronization
static TaskHandle_t weather_task_handle = NULL;
static QueueHandle_t weather_fetch_queue = NULL;
static SemaphoreHandle_t weather_data_mutex = NULL;
static bool weather_task_running = false;

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
            if (response) {
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
    
    // Open-Meteo geocoding API expects 'name' parameter for location search
    // For US zip codes, we can search directly or add country code
    snprintf(url, sizeof(url), "%s?name=%s&count=1&language=en&format=json", 
             OPEN_METEO_GEOCODING_API, encoded_zip);
    
    ESP_LOGI(TAG, "Geocoding URL: %s", url);

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
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
    };
    // Use certificate bundle if available (configured via sdkconfig)
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        
        ESP_LOGI(TAG, "HTTP Status = %d, response length = %d", status_code, response.len);
        
        if (status_code != 200 && response.len > 0) {
            ESP_LOGE(TAG, "Geocoding API error. Response: %.*s", response.len, response.buffer);
        }
        
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
            if (response) {
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
    const char *temp_unit_str = (temp_unit == WEATHER_TEMP_FAHRENHEIT) ? "fahrenheit" : "celsius";
    snprintf(url, sizeof(url), 
             "%s?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code&temperature_unit=%s&timezone=auto",
             OPEN_METEO_FORECAST_API, latitude, longitude, temp_unit_str);

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
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
    };
    // Use certificate bundle if available (configured via sdkconfig)
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        
        ESP_LOGI(TAG, "HTTP Status = %d, response length = %d", status_code, response.len);
        
        if (status_code != 200 && response.len > 0) {
            ESP_LOGE(TAG, "Weather API error. Response: %.*s", response.len, response.buffer);
        }
        
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

// Weather fetch task (runs HTTP operations in background)
static void weather_fetch_task(void *pvParameters)
{
    (void)pvParameters;
    
    while (weather_task_running) {
        // Wait for fetch request
        bool fetch_requested = false;
        if (xQueueReceive(weather_fetch_queue, &fetch_requested, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (!fetch_requested) {
                continue;
            }
            
            if (strlen(zip_code) == 0) {
                ESP_LOGW(TAG, "No zip code configured, skipping fetch");
                continue;
            }
            
            // Geocode zip code if needed
            if (cached_latitude == 0.0f && cached_longitude == 0.0f) {
                if (geocode_zip_code(zip_code, &cached_latitude, &cached_longitude) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to geocode zip code: %s", zip_code);
                    continue;
                }
            }
            
            // Fetch weather data
            weather_data_t weather = {0};
            if (fetch_weather_data(cached_latitude, cached_longitude, &weather) == ESP_OK) {
                // Update cached data with mutex protection
                if (xSemaphoreTake(weather_data_mutex, portMAX_DELAY) == pdTRUE) {
                    memcpy(&cached_weather, &weather, sizeof(weather_data_t));
                    xSemaphoreGive(weather_data_mutex);
                }
                // Notify UI to refresh weather widget
                extern void ui_state_refresh(void);
                ui_state_refresh();
            }
        }
    }
    
    vTaskDelete(NULL);
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

static void load_temp_unit(void)
{
    if (sd_db_is_ready()) {
        char temp_unit_str[16];
        if (sd_db_get_string("weather_temp_unit", temp_unit_str, sizeof(temp_unit_str)) == ESP_OK) {
            if (strcmp(temp_unit_str, "fahrenheit") == 0) {
                temp_unit = WEATHER_TEMP_FAHRENHEIT;
            } else {
                temp_unit = WEATHER_TEMP_CELSIUS;  // Default to Celsius
            }
            ESP_LOGI(TAG, "Loaded temperature unit: %s", temp_unit_str);
        }
    }
}

static void save_temp_unit(void)
{
    if (sd_db_is_ready()) {
        const char *temp_unit_str = (temp_unit == WEATHER_TEMP_FAHRENHEIT) ? "fahrenheit" : "celsius";
        sd_db_set_string("weather_temp_unit", temp_unit_str);
        sd_db_save();
        ESP_LOGI(TAG, "Saved temperature unit to storage: %s", temp_unit_str);
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
    load_temp_unit();
    memset(&cached_weather, 0, sizeof(cached_weather));
    
    // Create mutex for thread-safe access to cached weather data
    weather_data_mutex = xSemaphoreCreateMutex();
    if (weather_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create weather data mutex");
        return;
    }
    
    // Create queue for fetch requests
    weather_fetch_queue = xQueueCreate(5, sizeof(bool));
    if (weather_fetch_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create weather fetch queue");
        return;
    }
    
    // Create background task for HTTP operations
    weather_task_running = true;
    BaseType_t ret = xTaskCreate(
        weather_fetch_task,
        "weather_fetch",
        8192,  // Stack size
        NULL,
        5,     // Priority
        &weather_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create weather fetch task");
        weather_task_running = false;
        return;
    }
    
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
    
    // Request fetch from background task (non-blocking)
    bool fetch_request = true;
    if (weather_fetch_queue != NULL) {
        xQueueSend(weather_fetch_queue, &fetch_request, 0);  // Non-blocking
    }
    
    // Return cached data immediately (if available)
    return weather_service_get_cached(data);
}

esp_err_t weather_service_get_cached(weather_data_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get cached data with mutex protection
    if (weather_data_mutex != NULL && xSemaphoreTake(weather_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool valid = cached_weather.valid;
        if (valid) {
            // Check if cache is still valid (within timeout)
            uint32_t now = (uint32_t)time(NULL);
            if (now - cached_weather.timestamp > WEATHER_CACHE_TIMEOUT_SEC) {
                ESP_LOGI(TAG, "Weather cache expired");
                valid = false;
            } else {
                memcpy(data, &cached_weather, sizeof(weather_data_t));
            }
        }
        xSemaphoreGive(weather_data_mutex);
        
        return valid ? ESP_OK : ESP_FAIL;
    }
    
    return ESP_FAIL;
}

esp_err_t weather_service_set_temp_unit(weather_temp_unit_t unit)
{
    if (unit != WEATHER_TEMP_CELSIUS && unit != WEATHER_TEMP_FAHRENHEIT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    temp_unit = unit;
    save_temp_unit();
    
    // Clear cached weather data so it will be refetched with new unit
    if (weather_data_mutex != NULL && xSemaphoreTake(weather_data_mutex, portMAX_DELAY) == pdTRUE) {
        memset(&cached_weather, 0, sizeof(weather_data_t));
        cached_latitude = 0.0f;
        cached_longitude = 0.0f;
        xSemaphoreGive(weather_data_mutex);
    }
    
    ESP_LOGI(TAG, "Temperature unit set to: %s", (unit == WEATHER_TEMP_FAHRENHEIT) ? "Fahrenheit" : "Celsius");
    return ESP_OK;
}

weather_temp_unit_t weather_service_get_temp_unit(void)
{
    return temp_unit;
}

