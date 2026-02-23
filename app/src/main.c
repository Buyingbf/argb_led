#include <errno.h>
#include <string.h>

#define LOG_LEVEL 4
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#include <rgbw_strip.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/util.h>

#define STRIP_NODE		DT_ALIAS(rgbw_strip)
#define STRIP_NUM_PIXELS	DT_PROP(DT_ALIAS(rgbw_strip), chain_length)
#define CURSOR_SIZE     12

#define DELAY_TIME K_MSEC(1000)

#define RGBW(_r, _g, _b, _w) { .r = (_r), .g = (_g), .b = (_b) , .w = (_w)}

static const struct led_rgbw colors[] = {
	RGBW(0x0f, 0x00, 0x00, 0x00), /* red */
	RGBW(0x00, 0x0f, 0x00, 0x00), /* green */
	RGBW(0x00, 0x00, 0x0f, 0x00), /* blue */
    RGBW(0x00, 0x00, 0x00, 0x0f), /* white */
};

struct led_rgbw pixels[STRIP_NUM_PIXELS];

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);

int main()
{
	size_t cursor = 0, color = 0;
	int rc;

	if (device_is_ready(strip)) {
		LOG_INF("Found LED strip device %s", strip->name);
	} else {
		LOG_ERR("LED strip device %s is not ready", strip->name);
		return 0;
	}

	LOG_INF("Displaying pattern on strip");

	color = 3;
	while (1) {
        memset(pixels, 0x00, sizeof(pixels));
		// memset(&pixels, 0x01, sizeof(pixels));
        // memset(pixels, 0xff, sizeof(struct led_rgb) * 12);
        // for (int i=0; i<CURSOR_SIZE; i++) {
            // memcpy(&pixels[cursor+i], &colors[color], sizeof(struct led_rgb));
        // }
        memcpy(&pixels[cursor], &colors[color], sizeof(struct led_rgbw));
		rc = rgbw_strip_update_rgbw(strip, pixels, STRIP_NUM_PIXELS);

		if (rc) {
			LOG_ERR("couldn't update strip: %d", rc);
		}

		cursor++;
		if (cursor >= STRIP_NUM_PIXELS) {
			cursor = 0;
		// 	color++;
		// 	if (color == ARRAY_SIZE(colors)) {
		// 		color = 0;
		// 	}
		}

		k_sleep(DELAY_TIME);
	}

    return 0;
}