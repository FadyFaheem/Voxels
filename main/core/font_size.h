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
    FONT_SIZE_SMALL = 0,      // 12px
    FONT_SIZE_NORMAL = 1,     // 16px
    FONT_SIZE_MEDIUM = 2,     // 18px
    FONT_SIZE_LARGE = 3,      // 20px
    FONT_SIZE_XLARGE = 4,     // 24px
    FONT_SIZE_XXLARGE = 5,    // 26px
    FONT_SIZE_HUGE = 6        // 48px
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

