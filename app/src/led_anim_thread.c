#define LOG_LEVEL 4
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(led_anim_thread);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#include <rgbw_strip.h>
#include "led_anim_thread.h"

#define STRIP_NODE		DT_ALIAS(rgbw_strip)
#define STRIP_NUM_PIXELS	DT_PROP(DT_ALIAS(rgbw_strip), chain_length)
#define CURSOR_SIZE     12

#define RGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b)}
#define RGBW(_r, _g, _b, _w) { .r = (_r), .g = (_g), .b = (_b) , .w = (_w)}
#define STEP_MS 16 // 60fps = 16.67ms

static const led_rgbw preset_colors[] = {
	RGBW(0x0f, 0x00, 0x00, 0x00), /* red */
	RGBW(0x00, 0x0f, 0x00, 0x00), /* green */
	RGBW(0x00, 0x00, 0x0f, 0x00), /* blue */
    RGBW(0x0f, 0x0f, 0x0f, 0x0f), /* white */
};

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
static led_rgbw pixels[STRIP_NUM_PIXELS];


static led_hsv current_hsv;             // Current HSV color state; basis for all calculations
static led_rgbw base_rgb, scaled_rgbw;  // Base RGB color converted from HSV at full brightness, then scaled by current brightness level for final output
static uint8_t brightness;
bool no_white_component;          

static led_rgbw current_color; // Current and new (scaled) RGBW colors for the LED strip; white component extracted from RGB values

static uint8_t duration;

K_MSGQ_DEFINE(led_message_queue, sizeof(struct led_msg), 10, 1); // Why alignment?

void led_anim_thread(void *arg1, void *arg2, void *arg3)
{
    /* Initialize RGBW strip*/
	if (device_is_ready(strip))
	{
		LOG_INF("Found LED strip device %s", strip->name);
	} 
    else
	{
		LOG_ERR("LED strip device %s is not ready", strip->name);
		return;
	}
    current_color = (led_rgbw){0x3f, 0x3f, 0x3f, 0x3f};
    update_rgbw_strip();

    struct led_msg msg;
    struct led_rgbw new_color;
    while (1)
    {
        k_msgq_get(&led_message_queue, &msg, K_FOREVER); // Wait indefinitely for a message
        // LOG_INF("Received LED message: command=%d, new_color=(%d, %d, %d, %d), new_brightness=%d",
        //         msg.command, msg.new_color.r, msg.new_color.g, msg.new_color.b, msg.new_color.w, msg.new_brightness);

        if (msg.params & LED_PARAM_BRIGHTNESS)
        {
            LOG_INF("Updating brightness to %d", msg.new_hsv.v);
            brightness = msg.new_hsv.v;
            scale_brightness(&scaled_rgbw, &base_rgb, brightness);                  // Scale RGB values by current brightness; no white component
            rgb2rgbw(&scaled_rgbw, &new_color);  
            // if (msg.new_hsv.v == brightness)
            // {
            //     continue; // No change in brightness, skip
            // }
        }
        if (msg.params & LED_PARAM_COLOR)
        {
            if (memcmp(&current_hsv, &msg.new_hsv, sizeof(led_hsv)) == 0)
            {
                continue; // No change in color, skip
            }

            // LOG_INF("Updating color to (%d, %d, %d) with brightness %d", msg.new_hsv.h, msg.new_hsv.s, msg.new_hsv.v, msg.new_brightness);
            current_hsv = msg.new_hsv;
            brightness = msg.new_hsv.v;

            hsv2rgb(&base_rgb, &(led_hsv) {current_hsv.h, current_hsv.s, 255U});    // Convert HSV to RGB with full brightness for accurate scaling later on
            scale_brightness(&scaled_rgbw, &base_rgb, brightness);                  // Scale RGB values by current brightness; no white component
            rgb2rgbw(&scaled_rgbw, &new_color);                                      // Convert scaled RGB to RGBW format for strip; white component extracted from RGB values
        }

        if (msg.params & LED_PARAM_BRIGHTNESS || msg.params & LED_PARAM_COLOR)
        {
            switch (msg.command)
            {
                case SET:
                    set_color_rgb(&new_color);
                    break;
                case FADE:
                    fade_color_rgb(&new_color, msg.duration); // Example: fade over duration ms
                    break;
            }
        }

        // if (msg.params & LED_PARAM_COLOR)
        // {
        //     LOG_INF("Updating color to (%d, %d, %d, %d)", msg.new_color.r, msg.new_color.g, msg.new_color.b, msg.new_color.w);
        //     new_color = msg.new_color; // WIP
        // }
        // if (msg.params & LED_PARAM_BRIGHTNESS)
        // {
        //     LOG_INF("Updating brightness to %d", msg.new_brightness);
        //     new_brightness = msg.new_brightness; // WIP
        // }
    }
}

static uint32_t fn (const uint8_t h, const int n)
{
    const int kZero = 0 << 8;
    const int kOne  = 1 << 8;
    const int kFour = 4 << 8;
    const int kSix  = 6 << 8;

    const int k = ((n << 8) + 6*h) % kSix;
    const int k2 = kFour - k;
    return MAX(kZero, MIN(kOne, MIN(k, k2)));
}

void hsv2rgb(led_rgbw *rgbw, const led_hsv *hsv)
{
    const uint8_t chroma = (hsv->v * hsv->s) / 255;

    rgbw->r = hsv->v - ((chroma * fn(hsv->h, 5)) >> 8);
    rgbw->g = hsv->v - ((chroma * fn(hsv->h, 3)) >> 8);
    rgbw->b = hsv->v - ((chroma * fn(hsv->h, 1)) >> 8);
    rgbw->w = 0; // No white component in HSV model, set to 0 for now

}

static void fade_color_rgb(const led_rgbw *new_color, const uint32_t duration_ms)
{
	if (duration_ms <= STEP_MS) {
		set_color_rgb(new_color);
		return; // Duration too short for fade, set color immediately
	}

	int64_t start_time = k_uptime_get();
	int64_t elapsed_time = 0;
	led_rgbw start = current_color;
	while(elapsed_time < duration_ms)
	{
		k_sleep(K_MSEC(STEP_MS)); // 60fps = 16.67ms
		elapsed_time = k_uptime_get() - start_time;
		if (elapsed_time > duration_ms) {
			elapsed_time = duration_ms; // Cap elapsed time to duration
		}
		current_color.r = start.r + ((new_color->r - start.r) * elapsed_time) / duration_ms;
		current_color.g = start.g + ((new_color->g - start.g) * elapsed_time) / duration_ms;
		current_color.b = start.b + ((new_color->b - start.b) * elapsed_time) / duration_ms;
		current_color.w = start.w + ((new_color->w - start.w) * elapsed_time) / duration_ms;
        update_rgbw_strip();
	}
	current_color = *new_color; // Ensure final color is set precisely
	update_rgbw_strip();
}

static void set_color_rgb(const led_rgbw *new_color)
{
    memcpy(&current_color, new_color, sizeof(led_rgbw));
    update_rgbw_strip();
}

static void scale_brightness(led_rgbw *scaled_color, const led_rgbw *color, const uint8_t brightness)
{
    scaled_color->r = (color->r * brightness) / 255;
    scaled_color->g = (color->g * brightness) / 255;
    scaled_color->b = (color->b * brightness) / 255;
    scaled_color->w = (color->w * brightness) / 255;
}

static void rgb2rgbw(const led_rgbw *rgb, led_rgbw *rgbw)
{
    if (no_white_component)
    {
        *rgbw = *rgb; // If no white component, just copy RGB values and set W to 0
        rgbw->w = 0;
        return;
    }

    uint8_t min_component = MIN(rgb->r, MIN(rgb->g, rgb->b));
    rgbw->w = min_component;
    rgbw->r = rgb->r - min_component;
    rgbw->g = rgb->g - min_component;
    rgbw->b = rgb->b - min_component;
}

static void update_rgbw_strip(void)
{
	int rc;
	memset(pixels, 0x00, sizeof(pixels));

	// led_rgbw scaled_color;
	// brightness_scale_color(&scaled_color, &color, brightness);

	// for (size_t i = 0; i < 4; i++) {
	// 	memcpy(&pixels[i], &scaled_color, sizeof(led_rgbw));
	// }

	for (size_t i = 0; i < 4; i++) {
		memcpy(&pixels[i], &current_color, sizeof(led_rgbw));
	}

	rc = rgbw_strip_update_rgbw(strip, pixels, STRIP_NUM_PIXELS);

	if (rc)
	{
		LOG_ERR("Couldn't update strip: %d", rc);
	}
}
