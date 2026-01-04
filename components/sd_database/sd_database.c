#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include "sd_database.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "sd_database";

// Database file path
#define DB_FILE_PATH    BSP_SD_MOUNT_POINT "/voxels.db"
#define DB_MARKER_FILE  BSP_SD_MOUNT_POINT "/.voxels_init"
#define MAX_LINE_LEN    256
#define MAX_ENTRIES     100

// In-memory database cache
typedef struct {
    char key[64];
    char value[128];
} db_entry_t;

static db_entry_t db_cache[MAX_ENTRIES];
static int db_entry_count = 0;
static sd_db_status_t db_status = SD_DB_NOT_PRESENT;
static bool db_modified = false;

// Forward declarations
static esp_err_t load_database(void);
static esp_err_t create_empty_database(void);
static int find_entry(const char *key);

sd_db_status_t sd_db_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card database...");
    
    // Try to mount SD card
    esp_err_t ret = bsp_sdcard_mount();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card not present or mount failed: %s", esp_err_to_name(ret));
        db_status = SD_DB_NOT_PRESENT;
        return db_status;
    }
    
    ESP_LOGI(TAG, "SD card mounted successfully");
    
    // Check if database marker file exists
    struct stat st;
    if (stat(DB_MARKER_FILE, &st) != 0) {
        ESP_LOGW(TAG, "Database not initialized - marker file missing");
        db_status = SD_DB_NOT_INITIALIZED;
        return db_status;  // Return early - let caller decide to format
    }
    
    // Load existing database
    ret = load_database();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load database - needs initialization");
        db_status = SD_DB_NOT_INITIALIZED;
        return db_status;  // Return early - let caller decide to format
    }
    
    db_status = SD_DB_READY;
    ESP_LOGI(TAG, "Database ready with %d entries", db_entry_count);
    
    return db_status;
}

sd_db_status_t sd_db_format_and_init(void)
{
    ESP_LOGI(TAG, "Formatting SD card and initializing database...");
    
    if (sd_db_wipe() != ESP_OK) {
        db_status = SD_DB_ERROR;
        return db_status;
    }
    
    esp_err_t ret = load_database();
    if (ret == ESP_OK) {
        db_status = SD_DB_READY;
        ESP_LOGI(TAG, "Database ready after format");
    } else {
        db_status = SD_DB_ERROR;
        ESP_LOGE(TAG, "Database initialization failed after format");
    }
    
    return db_status;
}

bool sd_db_is_ready(void)
{
    return db_status == SD_DB_READY;
}

sd_db_status_t sd_db_get_status(void)
{
    return db_status;
}

esp_err_t sd_db_wipe(void)
{
    ESP_LOGI(TAG, "Wiping SD card database...");
    
    // Remove all files in mount point (simple approach)
    DIR *dir = opendir(BSP_SD_MOUNT_POINT);
    if (dir) {
        struct dirent *entry;
        char filepath[128];
        
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {  // Regular file
                snprintf(filepath, sizeof(filepath), "%s/%s", BSP_SD_MOUNT_POINT, entry->d_name);
                ESP_LOGI(TAG, "Removing: %s", filepath);
                remove(filepath);
            }
        }
        closedir(dir);
    }
    
    // Clear in-memory cache
    db_entry_count = 0;
    memset(db_cache, 0, sizeof(db_cache));
    db_modified = false;
    
    // Create empty database
    return create_empty_database();
}

static esp_err_t create_empty_database(void)
{
    ESP_LOGI(TAG, "Creating empty database...");
    
    // Create database file
    FILE *f = fopen(DB_FILE_PATH, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create database file");
        return ESP_FAIL;
    }
    fprintf(f, "# Voxels Database v1.0\n");
    fprintf(f, "# Format: key=value\n");
    fclose(f);
    
    // Create marker file
    f = fopen(DB_MARKER_FILE, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create marker file");
        return ESP_FAIL;
    }
    fprintf(f, "initialized\n");
    fclose(f);
    
    ESP_LOGI(TAG, "Empty database created");
    return ESP_OK;
}

static esp_err_t load_database(void)
{
    ESP_LOGI(TAG, "Loading database from %s", DB_FILE_PATH);
    
    FILE *f = fopen(DB_FILE_PATH, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "Database file not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    db_entry_count = 0;
    char line[MAX_LINE_LEN];
    
    while (fgets(line, sizeof(line), f) != NULL && db_entry_count < MAX_ENTRIES) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        
        // Remove newline
        line[strcspn(line, "\r\n")] = 0;
        
        // Parse key=value
        char *eq = strchr(line, '=');
        if (eq != NULL) {
            *eq = '\0';
            strncpy(db_cache[db_entry_count].key, line, sizeof(db_cache[0].key) - 1);
            strncpy(db_cache[db_entry_count].value, eq + 1, sizeof(db_cache[0].value) - 1);
            db_entry_count++;
        }
    }
    
    fclose(f);
    ESP_LOGI(TAG, "Loaded %d entries from database", db_entry_count);
    return ESP_OK;
}

static int find_entry(const char *key)
{
    for (int i = 0; i < db_entry_count; i++) {
        if (strcmp(db_cache[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

esp_err_t sd_db_set_string(const char *key, const char *value)
{
    if (!sd_db_is_ready() || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int idx = find_entry(key);
    if (idx >= 0) {
        // Update existing entry
        strncpy(db_cache[idx].value, value, sizeof(db_cache[0].value) - 1);
    } else {
        // Add new entry
        if (db_entry_count >= MAX_ENTRIES) {
            ESP_LOGE(TAG, "Database full");
            return ESP_ERR_NO_MEM;
        }
        strncpy(db_cache[db_entry_count].key, key, sizeof(db_cache[0].key) - 1);
        strncpy(db_cache[db_entry_count].value, value, sizeof(db_cache[0].value) - 1);
        db_entry_count++;
    }
    
    db_modified = true;
    ESP_LOGD(TAG, "Set %s = %s", key, value);
    return ESP_OK;
}

esp_err_t sd_db_get_string(const char *key, char *value, size_t max_len)
{
    if (!sd_db_is_ready() || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int idx = find_entry(key);
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    strncpy(value, db_cache[idx].value, max_len - 1);
    value[max_len - 1] = '\0';
    return ESP_OK;
}

esp_err_t sd_db_set_int(const char *key, int value)
{
    char str_value[32];
    snprintf(str_value, sizeof(str_value), "%d", value);
    return sd_db_set_string(key, str_value);
}

esp_err_t sd_db_get_int(const char *key, int *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char str_value[32];
    esp_err_t ret = sd_db_get_string(key, str_value, sizeof(str_value));
    if (ret == ESP_OK) {
        *value = atoi(str_value);
    }
    return ret;
}

esp_err_t sd_db_delete(const char *key)
{
    if (!sd_db_is_ready() || key == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int idx = find_entry(key);
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Shift entries down
    for (int i = idx; i < db_entry_count - 1; i++) {
        db_cache[i] = db_cache[i + 1];
    }
    db_entry_count--;
    db_modified = true;
    
    ESP_LOGD(TAG, "Deleted key: %s", key);
    return ESP_OK;
}

bool sd_db_key_exists(const char *key)
{
    if (!sd_db_is_ready() || key == NULL) {
        return false;
    }
    return find_entry(key) >= 0;
}

esp_err_t sd_db_save(void)
{
    if (!sd_db_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!db_modified) {
        ESP_LOGD(TAG, "No changes to save");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Saving database to SD card...");
    
    FILE *f = fopen(DB_FILE_PATH, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open database file for writing");
        return ESP_FAIL;
    }
    
    fprintf(f, "# Voxels Database v1.0\n");
    fprintf(f, "# Format: key=value\n");
    
    for (int i = 0; i < db_entry_count; i++) {
        fprintf(f, "%s=%s\n", db_cache[i].key, db_cache[i].value);
    }
    
    fclose(f);
    db_modified = false;
    
    ESP_LOGI(TAG, "Database saved with %d entries", db_entry_count);
    return ESP_OK;
}

esp_err_t sd_db_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing SD card database...");
    
    // Save any pending changes
    if (db_modified) {
        sd_db_save();
    }
    
    // Clear cache
    db_entry_count = 0;
    memset(db_cache, 0, sizeof(db_cache));
    db_status = SD_DB_NOT_PRESENT;
    
    // Unmount SD card
    return bsp_sdcard_unmount();
}
