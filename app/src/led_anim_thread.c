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
static led_rgbw color;
static uint8_t brightness;

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
    color = (led_rgbw){0x0f, 0x0f, 0x0f, 0x0f};
    brightness = 0xff;
    update_rgbw_strip();

    struct led_msg msg;
    while (1)
    {
        k_msgq_get(&led_message_queue, &msg, K_FOREVER); // Wait indefinitely for a message
        LOG_INF("Received LED message: command=%d, new_color=(%d, %d, %d, %d), new_brightness=%d",
                msg.command, msg.new_color.r, msg.new_color.g, msg.new_color.b, msg.new_color.w, msg.new_brightness);
        switch (msg.command)
        {
            case SET:
                set_color(&msg.new_color);
                break;
            case FADE:
                fade_color(&msg.new_color, 1000); // Example: fade over 1 second
                break;
        }
    }
}

static void fade_color(const led_rgbw *new_color, uint32_t duration_ms)
{
	if (duration_ms <= STEP_MS) {
		set_color(new_color);
		return;
	}

	if (memcmp(&color, new_color, sizeof(led_rgbw)) == 0) {
		return; // No change in color
	}

	int64_t start_time = k_uptime_get();
	int64_t elapsed_time = 0;
	led_rgbw start = color;
	while(elapsed_time < duration_ms)
	{
		k_sleep(K_MSEC(STEP_MS)); // 60fps = 16.67ms
		elapsed_time = k_uptime_get() - start_time;
		if (elapsed_time > duration_ms) {
			elapsed_time = duration_ms; // Cap elapsed time to duration
		}
		color.r = start.r + ((new_color->r - start.r) * elapsed_time) / duration_ms;
		color.g = start.g + ((new_color->g - start.g) * elapsed_time) / duration_ms;
		color.b = start.b + ((new_color->b - start.b) * elapsed_time) / duration_ms;
		color.w = start.w + ((new_color->w - start.w) * elapsed_time) / duration_ms;
        update_rgbw_strip();
	}
	color = *new_color; // Ensure final color is set precisely
	update_rgbw_strip();
}

static void set_color(const led_rgbw *new_color)
{
    memcpy(&color, new_color, sizeof(led_rgbw));
    update_rgbw_strip();
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
		memcpy(&pixels[i], &color, sizeof(led_rgbw));
	}

	rc = rgbw_strip_update_rgbw(strip, pixels, STRIP_NUM_PIXELS);

	if (rc)
	{
		LOG_ERR("Couldn't update strip: %d", rc);
	}
}
