#define LOG_LEVEL 4
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(led_anim_thread);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#include <rgbw_strip.h>
#include "led_anim_thread_internal.h"

#define STRIP_NODE		DT_ALIAS(rgbw_strip)
#define STRIP_NUM_PIXELS	DT_PROP(DT_ALIAS(rgbw_strip), chain_length)
#define CURSOR_SIZE     12

#define RGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b)}
#define RGBW(_r, _g, _b, _w) { .r = (_r), .g = (_g), .b = (_b) , .w = (_w)}
#define STEP_MS 16 // 60fps = 16.67ms

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
static led_rgbw pixels[STRIP_NUM_PIXELS];
static const uint16_t gamma_table[256] = {
    0,     0,     1,     3,     7,    11,    17,    24,
   32,    41,    52,    65,    78,    93,   110,   128,
  148,   169,   192,   216,   242,   269,   298,   329,
  361,   395,   431,   468,   507,   548,   591,   635,
  681,   729,   778,   829,   882,   937,   994,  1052,
 1113,  1175,  1239,  1305,  1373,  1442,  1514,  1587,
 1662,  1739,  1818,  1899,  1982,  2067,  2154,  2243,
 2333,  2426,  2521,  2617,  2716,  2817,  2919,  3024,
 3130,  3239,  3350,  3462,  3577,  3694,  3813,  3934,
 4057,  4182,  4309,  4438,  4569,  4702,  4838,  4975,
 5115,  5257,  5401,  5546,  5695,  5845,  5997,  6152,
 6308,  6467,  6628,  6791,  6956,  7124,  7294,  7465,
 7639,  7815,  7994,  8174,  8357,  8542,  8729,  8919,
 9110,  9304,  9500,  9699,  9899, 10102, 10307, 10514,
10724, 10935, 11150, 11366, 11584, 11805, 12028, 12254,
12481, 12711, 12944, 13178, 13415, 13654, 13896, 14140,
14386, 14634, 14885, 15138, 15393, 15651, 15911, 16174,
16438, 16705, 16975, 17247, 17521, 17797, 18076, 18357,
18641, 18927, 19215, 19506, 19799, 20095, 20393, 20693,
20996, 21301, 21608, 21918, 22231, 22545, 22862, 23182,
23504, 23828, 24155, 24484, 24816, 25150, 25487, 25826,
26167, 26511, 26857, 27206, 27557, 27911, 28267, 28626,
28987, 29351, 29717, 30085, 30456, 30830, 31206, 31584,
31965, 32349, 32735, 33123, 33514, 33907, 34303, 34702,
35103, 35506, 35912, 36321, 36732, 37145, 37561, 37980,
38401, 38825, 39251, 39680, 40111, 40545, 40981, 41420,
41862, 42306, 42753, 43202, 43653, 44108, 44565, 45024,
45486, 45951, 46418, 46888, 47360, 47835, 48312, 48792,
49275, 49760, 50248, 50738, 51232, 51727, 52225, 52726,
53230, 53736, 54245, 54756, 55270, 55786, 56305, 56827,
57352, 57879, 58408, 58941, 59476, 60013, 60553, 61096,
61642, 62190, 62741, 63294, 63850, 64409, 64970, 65535,
};

// static led_hsv current_hsv;             // User-selected color in HSV
led_rgbw base_rgb;                      // Base RGB color converted from at full brightness 
static uint8_t master_brightness;       // Global brightness scaler, applied to final RGBW output

static led_rgbw current_color;          // Current color being displayed on the strip
static led_rgbw_16 linear_rgbw_16;      // Intermediate 16-bit RGBW values after brightness scaling
static led_rgbw_16 gamma_rgbw_16;       // Intermediate 16-bit RGBW values after gamma correction          
bool no_white_component;         // Flag to indicate use of white component; true: RGB only, false: RGBW with white component extracted from RGB values  
static uint8_t duration;                // Duration for animation effects

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
    master_brightness = 0xff;
    update_rgbw_strip();

    led_msg msg;
    led_rgbw new_color;
    while (1)
    {
        k_msgq_get(&led_message_queue, &msg, K_FOREVER); // Wait indefinitely for a message

        if (msg.params & LED_PARAM_BRIGHTNESS)
        {
            /* Temporarily disabled to allow for manual refresh */
            // if (msg.new_brightness == master_brightness)
            // {
            //     LOG_INF("Brightness unchanged at %d, skipping update", msg.new_brightness);
            //     continue; // No change in brightness, skip
            // }
            LOG_INF("Updating master brightness to %d", msg.new_brightness);
            master_brightness = msg.new_brightness;
        }

        if (msg.params & LED_PARAM_COLOR)
        {
            if (memcmp(&base_rgb, &msg.new_rgbw, sizeof(led_rgbw)) == 0)
            {
                continue; // No change in color, skip
            }
            base_rgb = msg.new_rgbw;
        }

        if (msg.params & LED_PARAM_BRIGHTNESS || msg.params & LED_PARAM_COLOR)
        {
            scale_brightness_16(&linear_rgbw_16, &base_rgb, master_brightness);
            gamma_correct_16(&gamma_rgbw_16, &linear_rgbw_16);
            rgb2rgbw(&linear_rgbw_16, &new_color);                                      // Convert to RGBW format, extracting white component if applicable
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

static void scale_brightness_16(led_rgbw_16 *scaled_color, const led_rgbw *color, const uint8_t brightness)
{
    scaled_color->r_16 = (color->r * brightness);
    scaled_color->g_16 = (color->g * brightness);
    scaled_color->b_16 = (color->b * brightness);
    scaled_color->w_16 = (color->w * brightness);
}

static void gamma_correct_16(led_rgbw_16 *corrected_color, const led_rgbw_16 *color)
{
    corrected_color->r_16 = (gamma_table[color->r_16 >> 8] * color->r_16) >> 16;
    corrected_color->g_16 = (gamma_table[color->g_16 >> 8] * color->g_16) >> 16;
    corrected_color->b_16 = (gamma_table[color->b_16 >> 8] * color->b_16) >> 16;
    corrected_color->w_16 = (gamma_table[color->w_16 >> 8] * color->w_16) >> 16;
}

static void rgb2rgbw(const led_rgbw *rgb, led_rgbw *rgbw)
{
    if (no_white_component && rgb->w == 0) // If white component is disabled and input color has no white component, just copy RGB values and set W to 0
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

	for (size_t i = 0; i < 4; i++) {
		memcpy(&pixels[i], &current_color, sizeof(led_rgbw));
	}

	rc = rgbw_strip_update_rgbw(strip, pixels, STRIP_NUM_PIXELS);

	if (rc)
	{
		LOG_ERR("Couldn't update strip: %d", rc);
	}
}
