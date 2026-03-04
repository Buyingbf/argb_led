#ifndef LED_ANIM_H
#define LED_ANIM_H

#include <rgbw_strip.h>

#define LED_PARAM_COLOR         BIT(0)
#define LED_PARAM_BRIGHTNESS  BIT(1)
#define LED_PARAM_DURATION    BIT(2)

typedef enum led_command
{
    SET,
    FADE,
} led_command;

/**
 * @brief Color structure using HSV color model; [0-255] range for each component
 */
typedef struct led_hsv {
    uint8_t h;
    uint8_t s;
    uint8_t v;
} led_hsv;

/**
 * @brief Message structure for LED animation message queue
 * @param command Transition to specified new values
 * @param params Bitmasked flag variable to signal which color parameters are to be updated; see LED_PARAM_*
 * @param new_hsv New HSV color values; only updated if LED_PARAM_COLOR bit is set in params
 * @param duration Duration of transition in ms; only updated if LED_PARAM_DURATION bit is set in params
 */
struct led_msg
{
    led_command command;
    uint8_t params;

    led_hsv new_hsv;
    uint32_t duration;
};

/**
 * @brief Thread entry function for LED animation
 */
void led_anim_thread(void *arg1, void *arg2, void *arg3);

/**
 * @brief Converts an HSV color to RGBW format
 * @param rgbw Pointer to dest RGBW color structure
 * @param hsv Pointer to src HSV color structure
 */
void hsv2rgb(led_rgbw *rgbw, const led_hsv *hsv);

/**
 * @brief Fades the current color to a new color over a specified duration
 * @param new_color Pointer to new color
 * @param duration_ms Duration of fade in milliseconds
 */
static void fade_color_rgb(const led_rgbw *new_color, const uint32_t duration_ms);

/**
 * @brief Scales an RGBW color's brightness by a specified factor
 * @param scaled_color Pointer to dest scaled color structure
 * @param color Pointer to src color structure
 * @param brightness Brightness level to scale to; [0-255] range 
 */
static void scale_brightness(led_rgbw *scaled_color, const led_rgbw *color, const uint8_t brightness);

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

#endif /* LED_ANIM_H */