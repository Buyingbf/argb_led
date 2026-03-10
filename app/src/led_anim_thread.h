#ifndef LED_ANIM_H
#define LED_ANIM_H

#include <rgbw_strip.h>

#define LED_PARAM_COLOR         BIT(0)
#define LED_PARAM_BRIGHTNESS  BIT(1)
#define LED_PARAM_DURATION    BIT(2)

typedef enum led_command
{
    NONE,
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
 * @param params Bitmasked flag variable to signal which color parameters are to be updated; see LED_PARAM_* defines
 * @param command Transition to specified new values
 * @param new_brightness New master brightness value; only updated if LED_PARAM_BRIGHTNESS bit is set in params
 * @param duration Duration of transition in ms; only updated if LED_PARAM_DURATION bit is set in params
 * @param new_hsv New HSV color values; only updated if LED_PARAM_COLOR bit is set in params (currently unused)
 * @param new_rgbw New RGBW color values; only updated if LED_PARAM_COLOR bit is set in params
 */
typedef struct led_msg
{
    uint8_t params;
    led_command command;
    uint8_t new_brightness;
    int64_t duration;

    union {
        led_hsv new_hsv;
        led_rgbw new_rgbw;
    };
} led_msg;

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
#endif /* LED_ANIM_H */