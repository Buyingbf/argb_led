#ifndef BT_LSS_H_
#define BT_LSS_H_

/**@file
 * @defgroup bt_lbs LED Strip Service API
 * @{
 * @brief API for the LED Strip Service (LSS).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/types.h>
#include <rgbw_strip.h>
#include <errno.h>

// Base UUID: xxxxxxxx-665a-46ec-a6eb-36715013ce98 (randomly generated)

/** @brief LSS Service UUID. */
#define BT_UUID_LSS_VAL BT_UUID_128_ENCODE(0x00000000, 0x665a, 0x46ec, 0xa6eb, 0x36715013ce98)

/** @brief LED Color Characteristic UUID. */
#define BT_UUID_LSS_COLOR_VAL BT_UUID_128_ENCODE(0x00000001, 0x665a, 0x46ec, 0xa6eb, 0x36715013ce98)

/** @brief LED Brightness Characteristic UUID. */
#define BT_UUID_LSS_BRIGHTNESS_VAL BT_UUID_128_ENCODE(0x00000002, 0x665a, 0x46ec, 0xa6eb, 0x36715013ce98)

#define BT_UUID_LSS BT_UUID_DECLARE_128(BT_UUID_LSS_VAL)
#define BT_UUID_LSS_COLOR BT_UUID_DECLARE_128(BT_UUID_LSS_COLOR_VAL)
#define BT_UUID_LSS_BRIGHTNESS BT_UUID_DECLARE_128(BT_UUID_LSS_BRIGHTNESS_VAL)

/** @brief Callback type for when the application wants to change color. */
typedef void (*color_cb_t)(const led_rgbw *color);

/** @brief Callback type for when the application wants to change brightness. */
typedef void (*brightness_cb_t)(const uint8_t *brightness);

/** @brief Application callback struct */
struct my_lss_cb {
	color_cb_t color_cb;            // Color change callback
	brightness_cb_t brightness_cb;  // Brightness change callback
};

/** @brief Initialize the LSS Service.
 *
 * This function registers application callback functions with the LED Strip
 * Service
 *
 * @param[in] callbacks Struct containing pointers to callback functions
 *			used by the service. This pointer can be NULL
 *			if no callback functions are defined.
 *
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int my_lss_init(struct my_lss_cb *callbacks);

/**
 * @brief Create a scaled color based on the input color and brightness.
 * 
 * @param scaled_color Pointer to struct where the scaled color is stored.
 * @param color Pointer to the original color struct.
 * @param brightness Brightness value (0-255) to scale the color by.
 * @return void
 */
void brightness_scale_color(led_rgbw *scaled_color, led_rgbw *color, uint8_t brightness);

#ifdef __cplusplus
}
#endif

#endif // BT_LSS_H_