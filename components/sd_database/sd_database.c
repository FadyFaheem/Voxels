#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include "sd_database.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "sd_database";

// Database file path (for SD card mode)
#define DB_FILE_PATH    BSP_SD_MOUNT_POINT "/voxels.db"
#define DB_MARKER_FILE  BSP_SD_MOUNT_POINT "/.voxels_init"
#define MAX_LINE_LEN    256
#define MAX_ENTRIES     100

// NVS namespace
#define NVS_NAMESPACE   "voxels_db"

// Storage mode
typedef enum {
    STORAGE_NONE,
    STORAGE_NVS,
    STORAGE_SD
} storage_mode_t;

// In-memory database cache
typedef struct {
    char key[64];
    char value[128];
} db_entry_t;

static db_entry_t db_cache[MAX_ENTRIES];
static int db_entry_count = 0;
static sd_db_status_t db_status = SD_DB_NOT_PRESENT;
static storage_mode_t storage_mode = STORAGE_NONE;
static bool db_modified = false;
static nvs_handle_t db_nvs_handle = 0;

// Forward declarations
static esp_err_t load_database_sd(void);
static esp_err_t load_database_nvs(void);
static esp_err_t create_empty_database(void);
static esp_err_t save_to_nvs(void);
static int find_entry(const char *key);

sd_db_status_t sd_db_init(void)
{
    ESP_LOGI(TAG, "Initializing database...");
    
    // First, try to mount SD card
    esp_err_t ret = bsp_sdcard_mount();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted successfully");
        
        // Check if database marker file exists
        struct stat st;
        if (stat(DB_MARKER_FILE, &st) == 0) {
            // Load existing database from SD
            ret = load_database_sd();
            if (ret == ESP_OK) {
                storage_mode = STORAGE_SD;
                db_status = SD_DB_READY;
                ESP_LOGI(TAG, "SD card database ready with %d entries", db_entry_count);
                return db_status;
            }
        }
        
        // SD card present but not initialized
        ESP_LOGW(TAG, "SD card needs initialization");
        db_status = SD_DB_NOT_INITIALIZED;
        return db_status;
    }
    
    // SD card not available - fall back to NVS
    ESP_LOGW(TAG, "SD card not available, using NVS flash storage");
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &db_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        db_status = SD_DB_ERROR;
        return db_status;
    }
    
    // Load from NVS
    ret = load_database_nvs();
    if (ret == ESP_OK) {
        storage_mode = STORAGE_NVS;
        db_status = SD_DB_READY;
        ESP_LOGI(TAG, "NVS database ready with %d entries", db_entry_count);
    } else {
        // NVS empty - that's OK, start fresh
        storage_mode = STORAGE_NVS;
        db_status = SD_DB_READY;
        db_entry_count = 0;
        ESP_LOGI(TAG, "NVS database initialized (empty)");
    }
    
    return db_status;
}

sd_db_status_t sd_db_format_and_init(void)
{
    ESP_LOGI(TAG, "Formatting SD card and initializing database...");
    
    if (sd_db_wipe() != ESP_OK) {
        db_status = SD_DB_ERROR;
        return db_status;
    }
    
    esp_err_t ret = load_database_sd();
    if (ret == ESP_OK) {
        storage_mode = STORAGE_SD;
        db_status = SD_DB_READY;
        ESP_LOGI(TAG, "SD card database ready after format");
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

const char* sd_db_get_storage_type(void)
{
    switch (storage_mode) {
        case STORAGE_SD:  return "SD Card";
        case STORAGE_NVS: return "NVS Flash";
        default:          return "None";
    }
}

esp_err_t sd_db_wipe(void)
{
    ESP_LOGI(TAG, "Wiping database...");
    
    if (storage_mode == STORAGE_NVS) {
        // Wipe NVS
        if (db_nvs_handle != 0) {
            nvs_erase_all(db_nvs_handle);
            nvs_commit(db_nvs_handle);
        }
    } else {
        // Wipe SD card
        DIR *dir = opendir(BSP_SD_MOUNT_POINT);
        if (dir) {
            struct dirent *entry;
            char filepath[128];
            
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_REG) {
                    snprintf(filepath, sizeof(filepath), "%s/%s", BSP_SD_MOUNT_POINT, entry->d_name);
                    ESP_LOGI(TAG, "Removing: %s", filepath);
                    remove(filepath);
                }
            }
            closedir(dir);
        }
    }
    
    // Clear in-memory cache
    db_entry_count = 0;
    memset(db_cache, 0, sizeof(db_cache));
    db_modified = false;
    
    // Create empty database (SD only)
    if (storage_mode != STORAGE_NVS) {
        return create_empty_database();
    }
    
    return ESP_OK;
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

static esp_err_t load_database_sd(void)
{
    ESP_LOGI(TAG, "Loading database from SD card: %s", DB_FILE_PATH);
    
    FILE *f = fopen(DB_FILE_PATH, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "Database file not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    db_entry_count = 0;
    char line[MAX_LINE_LEN];
    
    while (fgets(line, sizeof(line), f) != NULL && db_entry_count < MAX_ENTRIES) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        
        line[strcspn(line, "\r\n")] = 0;
        
        char *eq = strchr(line, '=');
        if (eq != NULL) {
            *eq = '\0';
            strncpy(db_cache[db_entry_count].key, line, sizeof(db_cache[0].key) - 1);
            strncpy(db_cache[db_entry_count].value, eq + 1, sizeof(db_cache[0].value) - 1);
            db_entry_count++;
        }
    }
    
    fclose(f);
    ESP_LOGI(TAG, "Loaded %d entries from SD card", db_entry_count);
    return ESP_OK;
}

static esp_err_t load_database_nvs(void)
{
    ESP_LOGI(TAG, "Loading database from NVS...");
    
    // Get entry count
    int32_t count = 0;
    esp_err_t ret = nvs_get_i32(db_nvs_handle, "_count", &count);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No entries in NVS");
        return ESP_ERR_NOT_FOUND;
    }
    
    db_entry_count = 0;
    
    // Load each entry
    for (int i = 0; i < count && i < MAX_ENTRIES; i++) {
        char key_name[16];
        char val_name[16];
        snprintf(key_name, sizeof(key_name), "_k%d", i);
        snprintf(val_name, sizeof(val_name), "_v%d", i);
        
        size_t key_len = sizeof(db_cache[0].key);
        size_t val_len = sizeof(db_cache[0].value);
        
        ret = nvs_get_str(db_nvs_handle, key_name, db_cache[db_entry_count].key, &key_len);
        if (ret != ESP_OK) continue;
        
        ret = nvs_get_str(db_nvs_handle, val_name, db_cache[db_entry_count].value, &val_len);
        if (ret != ESP_OK) continue;
        
        db_entry_count++;
    }
    
    ESP_LOGI(TAG, "Loaded %d entries from NVS", db_entry_count);
    return ESP_OK;
}

static esp_err_t save_to_nvs(void)
{
    ESP_LOGI(TAG, "Saving database to NVS...");
    
    // Clear existing entries first
    nvs_erase_all(db_nvs_handle);
    
    // Save count
    nvs_set_i32(db_nvs_handle, "_count", db_entry_count);
    
    // Save each entry
    for (int i = 0; i < db_entry_count; i++) {
        char key_name[16];
        char val_name[16];
        snprintf(key_name, sizeof(key_name), "_k%d", i);
        snprintf(val_name, sizeof(val_name), "_v%d", i);
        
        nvs_set_str(db_nvs_handle, key_name, db_cache[i].key);
        nvs_set_str(db_nvs_handle, val_name, db_cache[i].value);
    }
    
    nvs_commit(db_nvs_handle);
    db_modified = false;
    
    ESP_LOGI(TAG, "Database saved to NVS with %d entries", db_entry_count);
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
        strncpy(db_cache[idx].value, value, sizeof(db_cache[0].value) - 1);
    } else {
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
    
    if (storage_mode == STORAGE_NVS) {
        return save_to_nvs();
    }
    
    // Save to SD card
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
    
    ESP_LOGI(TAG, "Database saved to SD card with %d entries", db_entry_count);
    return ESP_OK;
}

esp_err_t sd_db_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing database...");
    
    // Save any pending changes
    if (db_modified) {
        sd_db_save();
    }
    
    // Close NVS if open
    if (db_nvs_handle != 0) {
        nvs_close(db_nvs_handle);
        db_nvs_handle = 0;
    }
    
    // Clear cache
    db_entry_count = 0;
    memset(db_cache, 0, sizeof(db_cache));
    db_status = SD_DB_NOT_PRESENT;
    storage_mode = STORAGE_NONE;
    
    // Unmount SD card if mounted
    bsp_sdcard_unmount();
    
    return ESP_OK;
}
