#ifndef LED_ANIM_H
#define LED_ANIM_H

#include <rgbw_strip.h>

typedef enum led_command
{
    SET,
    FADE,
} led_command;

struct led_msg
{
    led_rgbw new_color;
    uint8_t new_brightness;
    led_command command;
};

/**
 * @brief Thread entry function for LED animation
 */
void led_anim_thread(void *arg1, void *arg2, void *arg3);

/**
 * @brief Fades the current color to a new color over a specified duration
 * @param new_color Pointer to new color
 * @param duration_ms Duration of fade in milliseconds
 */
static void fade_color(const led_rgbw *new_color, uint32_t duration_ms);

/**
 * @brief Sets the current color immediately
 * @param new_color Pointer to new color
 */
static void set_color(const led_rgbw *new_color);

/**
 * @brief Updates the RGBW strip with the current color and brightness values
 */
static void update_rgbw_strip(void);

#endif /* LED_ANIM_H */