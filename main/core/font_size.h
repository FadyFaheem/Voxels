#pragma once

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Font size manager
 * Manages application-wide font size settings
 */

/**
 * @brief Font size presets
 */
typedef enum {
    FONT_SIZE_TINY = 0,       // Extra small (12px base)
    FONT_SIZE_SMALL = 1,      // Small (12-16px)
    FONT_SIZE_NORMAL = 2,     // Normal (16px base)
    FONT_SIZE_MEDIUM = 3,     // Medium (18px base)
    FONT_SIZE_MEDIUM_LARGE = 4, // Medium-Large (20px base)
    FONT_SIZE_LARGE = 5,      // Large (20-22px)
    FONT_SIZE_XLARGE = 6,     // Extra Large (24px base)
    FONT_SIZE_XXLARGE = 7,    // XX Large (26px base)
    FONT_SIZE_HUGE = 8,       // Huge (48px for main display)
    FONT_SIZE_GIANT = 9       // Giant (48px everywhere)
} font_size_preset_t;

/**
 * @brief Initialize font size manager
 */
void font_size_init(void);

/**
 * @brief Get current font size preset
 * @return Font size preset value
 */
font_size_preset_t font_size_get_preset(void);

/**
 * @brief Set font size preset
 * @param preset Font size preset
 */
void font_size_set_preset(font_size_preset_t preset);

/**
 * @brief Get font for small text (labels, status)
 * @return Pointer to LVGL font
 */
const lv_font_t* font_size_get_small(void);

/**
 * @brief Get font for normal text (body)
 * @return Pointer to LVGL font
 */
const lv_font_t* font_size_get_normal(void);

/**
 * @brief Get font for medium text (subheadings)
 * @return Pointer to LVGL font
 */
const lv_font_t* font_size_get_medium(void);

/**
 * @brief Get font for large text (headings)
 * @return Pointer to LVGL font
 */
const lv_font_t* font_size_get_large(void);

/**
 * @brief Get font for extra large text (main display)
 * @return Pointer to LVGL font
 */
const lv_font_t* font_size_get_xlarge(void);

/**
 * @brief Get font for huge text (clock, timer main display)
 * @return Pointer to LVGL font
 */
const lv_font_t* font_size_get_huge(void);

#ifdef __cplusplus
}
#endif

