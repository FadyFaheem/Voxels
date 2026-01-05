#include "font_size.h"
#include "sd_database.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "font_size";
static font_size_preset_t current_preset = FONT_SIZE_NORMAL; // Default to Normal (was index 1, now index 2)

// Font mapping based on preset
static const struct {
    const lv_font_t *small;
    const lv_font_t *normal;
    const lv_font_t *medium;
    const lv_font_t *large;
    const lv_font_t *xlarge;
    const lv_font_t *huge;
} font_map[] = {
    // TINY preset (0)
    {
        .small = &lv_font_montserrat_12,
        .normal = &lv_font_montserrat_12,
        .medium = &lv_font_montserrat_12,
        .large = &lv_font_montserrat_16,
        .xlarge = &lv_font_montserrat_18,
        .huge = &lv_font_montserrat_20
    },
    // SMALL preset (1)
    {
        .small = &lv_font_montserrat_12,
        .normal = &lv_font_montserrat_14,
        .medium = &lv_font_montserrat_16,
        .large = &lv_font_montserrat_18,
        .xlarge = &lv_font_montserrat_20,
        .huge = &lv_font_montserrat_24
    },
    // NORMAL preset (2) - default
    {
        .small = &lv_font_montserrat_16,
        .normal = &lv_font_montserrat_16,
        .medium = &lv_font_montserrat_18,
        .large = &lv_font_montserrat_20,
        .xlarge = &lv_font_montserrat_24,
        .huge = &lv_font_montserrat_48
    },
    // MEDIUM preset (3)
    {
        .small = &lv_font_montserrat_18,
        .normal = &lv_font_montserrat_18,
        .medium = &lv_font_montserrat_20,
        .large = &lv_font_montserrat_22,
        .xlarge = &lv_font_montserrat_26,
        .huge = &lv_font_montserrat_48
    },
    // MEDIUM_LARGE preset (4)
    {
        .small = &lv_font_montserrat_18,
        .normal = &lv_font_montserrat_20,
        .medium = &lv_font_montserrat_20,
        .large = &lv_font_montserrat_22,
        .xlarge = &lv_font_montserrat_24,
        .huge = &lv_font_montserrat_48
    },
    // LARGE preset (5)
    {
        .small = &lv_font_montserrat_20,
        .normal = &lv_font_montserrat_20,
        .medium = &lv_font_montserrat_22,
        .large = &lv_font_montserrat_24,
        .xlarge = &lv_font_montserrat_26,
        .huge = &lv_font_montserrat_48
    },
    // XLARGE preset (6)
    {
        .small = &lv_font_montserrat_22,
        .normal = &lv_font_montserrat_24,
        .medium = &lv_font_montserrat_24,
        .large = &lv_font_montserrat_26,
        .xlarge = &lv_font_montserrat_48,
        .huge = &lv_font_montserrat_48
    },
    // XXLARGE preset (7)
    {
        .small = &lv_font_montserrat_24,
        .normal = &lv_font_montserrat_26,
        .medium = &lv_font_montserrat_28,
        .large = &lv_font_montserrat_32,
        .xlarge = &lv_font_montserrat_40,
        .huge = &lv_font_montserrat_48
    },
    // HUGE preset (8)
    {
        .small = &lv_font_montserrat_26,
        .normal = &lv_font_montserrat_30,
        .medium = &lv_font_montserrat_36,
        .large = &lv_font_montserrat_40,
        .xlarge = &lv_font_montserrat_48,
        .huge = &lv_font_montserrat_48
    },
    // GIANT preset (9)
    {
        .small = &lv_font_montserrat_48,
        .normal = &lv_font_montserrat_48,
        .medium = &lv_font_montserrat_48,
        .large = &lv_font_montserrat_48,
        .xlarge = &lv_font_montserrat_48,
        .huge = &lv_font_montserrat_48
    }
};

// Forward declaration for save_font_size (used in load_font_size)
static void save_font_size(void);

static void load_font_size(void)
{
    if (sd_db_is_ready()) {
        int preset = 0;
        if (sd_db_get_int("font_size_preset", &preset) == ESP_OK) {
            // Migration: old presets (0-6) map to new presets
            // Old: 0=SMALL, 1=NORMAL, 2=MEDIUM, 3=LARGE, 4=XLARGE, 5=XXLARGE, 6=HUGE
            // New: 0=TINY, 1=SMALL, 2=NORMAL, 3=MEDIUM, 4=MEDIUM_LARGE, 5=LARGE, 6=XLARGE, 7=XXLARGE, 8=HUGE, 9=GIANT
            if (preset >= 0 && preset <= 6) {
                // Map old values to semantically equivalent new values
                font_size_preset_t new_preset;
                switch (preset) {
                    case 0: new_preset = FONT_SIZE_SMALL; break;      // Old SMALL -> New SMALL
                    case 1: new_preset = FONT_SIZE_NORMAL; break;    // Old NORMAL -> New NORMAL
                    case 2: new_preset = FONT_SIZE_MEDIUM; break;    // Old MEDIUM -> New MEDIUM
                    case 3: new_preset = FONT_SIZE_LARGE; break;     // Old LARGE -> New LARGE
                    case 4: new_preset = FONT_SIZE_XLARGE; break;    // Old XLARGE -> New XLARGE
                    case 5: new_preset = FONT_SIZE_XXLARGE; break;   // Old XXLARGE -> New XXLARGE
                    case 6: new_preset = FONT_SIZE_HUGE; break;      // Old HUGE -> New HUGE
                    default: new_preset = FONT_SIZE_NORMAL; break;
                }
                current_preset = new_preset;
                ESP_LOGI(TAG, "Migrated font size preset from %d to %d", preset, current_preset);
                // Save migrated value
                save_font_size();
            } else if (preset >= 0 && preset <= FONT_SIZE_GIANT) {
                // New preset values (7-9) or already migrated
                current_preset = (font_size_preset_t)preset;
                ESP_LOGI(TAG, "Loaded font size preset: %d", current_preset);
            }
        }
    }
}

static void save_font_size(void)
{
    if (sd_db_is_ready()) {
        sd_db_set_int("font_size_preset", (int)current_preset);
        sd_db_save();
        ESP_LOGI(TAG, "Saved font size preset: %d", current_preset);
    }
}

void font_size_init(void)
{
    load_font_size();
    ESP_LOGI(TAG, "Font size manager initialized with preset: %d", current_preset);
}

font_size_preset_t font_size_get_preset(void)
{
    return current_preset;
}

void font_size_set_preset(font_size_preset_t preset)
{
    if (preset < 0 || preset > FONT_SIZE_GIANT) {
        ESP_LOGW(TAG, "Invalid font size preset: %d", preset);
        return;
    }
    
    current_preset = preset;
    save_font_size();
    ESP_LOGI(TAG, "Font size preset changed to: %d", current_preset);
}

const lv_font_t* font_size_get_small(void)
{
    return font_map[current_preset].small;
}

const lv_font_t* font_size_get_normal(void)
{
    return font_map[current_preset].normal;
}

const lv_font_t* font_size_get_medium(void)
{
    return font_map[current_preset].medium;
}

const lv_font_t* font_size_get_large(void)
{
    return font_map[current_preset].large;
}

const lv_font_t* font_size_get_xlarge(void)
{
    return font_map[current_preset].xlarge;
}

const lv_font_t* font_size_get_huge(void)
{
    return font_map[current_preset].huge;
}

