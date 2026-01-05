#include "time_sync.h"
#include "sd_database.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <string.h>
#include <time.h>

static const char *TAG = "time_sync";
static bool time_synced = false;
static char timezone[64] = "UTC0";

static void load_timezone(void)
{
    if (sd_db_is_ready()) {
        char saved_tz[64];
        if (sd_db_get_string("timezone", saved_tz, sizeof(saved_tz)) == ESP_OK) {
            if (strlen(saved_tz) > 0) {
                strncpy(timezone, saved_tz, sizeof(timezone) - 1);
                timezone[sizeof(timezone) - 1] = '\0';
                ESP_LOGI(TAG, "Loaded timezone from storage: %s", timezone);
            }
        }
    }
}

static void save_timezone(void)
{
    if (sd_db_is_ready()) {
        sd_db_set_string("timezone", timezone);
        sd_db_save();
        ESP_LOGI(TAG, "Saved timezone to storage: %s", timezone);
    }
}

static void time_sync_notification_cb(struct timeval *tv)
{
    time_synced = true;
    ESP_LOGI(TAG, "Time synchronized: %s", ctime(&tv->tv_sec));
}

void time_sync_init(void)
{
    if (esp_sntp_enabled()) {
        ESP_LOGW(TAG, "SNTP already initialized");
        return;
    }
    
    // Load saved timezone
    load_timezone();
    
    ESP_LOGI(TAG, "Initializing SNTP");
    
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    
    // Set timezone
    setenv("TZ", timezone, 1);
    tzset();
    
    ESP_LOGI(TAG, "SNTP initialized with timezone %s, waiting for sync...", timezone);
}

bool time_sync_is_synced(void)
{
    return time_synced;
}

esp_err_t time_sync_get_time(time_t *now)
{
    if (!now) {
        return ESP_ERR_INVALID_ARG;
    }
    
    time(now);
    return ESP_OK;
}

esp_err_t time_sync_set_timezone(const char *tz)
{
    if (!tz) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(timezone, tz, sizeof(timezone) - 1);
    timezone[sizeof(timezone) - 1] = '\0';
    
    setenv("TZ", timezone, 1);
    tzset();
    
    // Save to persistent storage
    save_timezone();
    
    ESP_LOGI(TAG, "Timezone set to: %s", timezone);
    return ESP_OK;
}

const char* time_sync_get_timezone(void)
{
    return timezone;
}

