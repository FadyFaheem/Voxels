#include "font_size.h"
#include "sd_database.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "font_size";
static font_size_preset_t current_preset = FONT_SIZE_NORMAL;

// Font mapping based on preset
static const struct {
    const lv_font_t *small;
    const lv_font_t *normal;
    const lv_font_t *medium;
    const lv_font_t *large;
    const lv_font_t *xlarge;
    const lv_font_t *huge;
} font_map[] = {
    // SMALL preset
    {
        .small = &lv_font_montserrat_12,
        .normal = &lv_font_montserrat_12,
        .medium = &lv_font_montserrat_16,
        .large = &lv_font_montserrat_18,
        .xlarge = &lv_font_montserrat_20,
        .huge = &lv_font_montserrat_24
    },
    // NORMAL preset
    {
        .small = &lv_font_montserrat_16,
        .normal = &lv_font_montserrat_16,
        .medium = &lv_font_montserrat_18,
        .large = &lv_font_montserrat_20,
        .xlarge = &lv_font_montserrat_24,
        .huge = &lv_font_montserrat_48
    },
    // MEDIUM preset
    {
        .small = &lv_font_montserrat_18,
        .normal = &lv_font_montserrat_18,
        .medium = &lv_font_montserrat_20,
        .large = &lv_font_montserrat_22,
        .xlarge = &lv_font_montserrat_26,
        .huge = &lv_font_montserrat_48
    },
    // LARGE preset
    {
        .small = &lv_font_montserrat_20,
        .normal = &lv_font_montserrat_20,
        .medium = &lv_font_montserrat_22,
        .large = &lv_font_montserrat_24,
        .xlarge = &lv_font_montserrat_26,
        .huge = &lv_font_montserrat_48
    },
    // XLARGE preset
    {
        .small = &lv_font_montserrat_22,
        .normal = &lv_font_montserrat_24,
        .medium = &lv_font_montserrat_24,
        .large = &lv_font_montserrat_26,
        .xlarge = &lv_font_montserrat_48,
        .huge = &lv_font_montserrat_48
    },
    // XXLARGE preset
    {
        .small = &lv_font_montserrat_24,
        .normal = &lv_font_montserrat_26,
        .medium = &lv_font_montserrat_26,
        .large = &lv_font_montserrat_48,
        .xlarge = &lv_font_montserrat_48,
        .huge = &lv_font_montserrat_48
    },
    // HUGE preset
    {
        .small = &lv_font_montserrat_26,
        .normal = &lv_font_montserrat_48,
        .medium = &lv_font_montserrat_48,
        .large = &lv_font_montserrat_48,
        .xlarge = &lv_font_montserrat_48,
        .huge = &lv_font_montserrat_48
    }
};

static void load_font_size(void)
{
    if (sd_db_is_ready()) {
        int preset = 0;
        if (sd_db_get_int("font_size_preset", &preset) == ESP_OK) {
            if (preset >= 0 && preset <= FONT_SIZE_HUGE) {
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
    if (preset < 0 || preset > FONT_SIZE_HUGE) {
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

