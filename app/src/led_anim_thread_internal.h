#ifndef LED_ANIM_THREAD_INTERNAL_H
#define LED_ANIM_THREAD_INTERNAL_H

#include "led_anim_thread.h"

typedef struct led_rgbw_16 {
    uint16_t r_16;
    uint16_t g_16;
    uint16_t b_16;
    uint16_t w_16;
} led_rgbw_16;

/**
 * @brief Fades the current color to a new color over a specified duration
 * @param new_color Pointer to new color
 * @param duration_ms Duration of fade in milliseconds
 */
static void fade_color_rgb(const led_rgbw *new_color, const uint32_t duration_ms);

/**
 * @brief Scales an RGBW color's brightness by a specified factor
 * @param scaled_color Pointer to dest scaled 16-bit color structure
 * @param color Pointer to src color structure
 * @param brightness Brightness level to scale to; [0-255] range 
 */
static void scale_brightness_16(led_rgbw_16 *scaled_color, const led_rgbw *color, const uint8_t brightness);

/**
 * @brief Applies gamma correction to a 16-bitRGBW color
 * @param corrected_color Pointer to dest corrected color structure
 * @param color Pointer to src color structure
 */
static void gamma_correct_16(led_rgbw_16 *corrected_color, const led_rgbw_16 *color);

/**
 * @brief Converts an RGB color to RGBW format by extracting the white component
 * @param rgb Pointer to src RGB color structure (white component ignored)
 * @param rgbw Pointer to dest RGBW color structure (white component extracted from RGB values)
 */
static void rgb2rgbw(const led_rgbw *rgb, led_rgbw *rgbw);

/**
 * @brief Sets the current color immediately
 * @param new_color Pointer to new color
 */
static void set_color_rgb(const led_rgbw *new_color);

/**
 * @brief Updates the RGBW strip with the current color and brightness values
 */
static void update_rgbw_strip(void);


#endif // LED_ANIM_THREAD_INTERNAL_H