#define LOG_LEVEL 4
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(led_anim_thread);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <stdlib.h>

#include <rgbw_strip.h>
#include "led_anim_thread_internal.h"

#define STRIP_NODE		DT_ALIAS(rgbw_strip)
#define STRIP_NUM_PIXELS	DT_PROP(DT_ALIAS(rgbw_strip), chain_length)
#define CURSOR_SIZE     12

#define RGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b)}
#define RGBW(_r, _g, _b, _w) { .r = (_r), .g = (_g), .b = (_b) , .w = (_w)}
#define CAN_DITHER(channel) (rgbw_16_int(##channel##) < DITHER_THRESHOLD && current_state == DITHERING && dither_enabled)

#define FRAME_MS 16 // 60fps = 16.67ms
#define DITHER_THRESHOLD 30 // Threshold below which dithering is applied

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);

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
led_rgbw base_rgb = {0};                      // User-selected color in RGBW
static uint8_t master_brightness = 0xff;       // Global brightness scaler, applied to final RGBW output

bool dither_enabled = true;                 // Flag to enable/disable dithering globally; true: dithering enabled, false: dithering disabled (all colors treated as above threshold)
bool no_white_component = false;         // Flag to indicate use of white component; true: RGB only, false: RGBW with white component extracted from RGB values  
static led_state current_state;         
static led_command current_command;

// static led_rgbw current_color;          // Current color being displayed on the strip
static led_rgbw_16 pre_dithered_color_16;

static led_rgbw new_color, new_color_frac;

static int64_t start_time, elapsed_time;

static int64_t frame_time, total_frame_time;
static int64_t frames = 0;

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
    // current_state = STATIC;
    // current_command = NONE;
    // current_color = (led_rgbw) RGBW(0, 0, 0, 0);
    // update_rgbw_strip();
    k_msgq_put(&led_message_queue, &(struct led_msg){
        .new_rgbw = (led_rgbw) RGBW(0, 0, 0, 0),
        .command = SET,
        .params = LED_PARAM_COLOR,
    }, K_FOREVER);

    led_rgbw_16 start_color_16;
    led_rgbw_16 linear_rgbw_16, gamma_rgbw_16;
    led_msg msg; 
    uint32_t duration_ms;
   
    while (1)
    {

        /* Wait indefinitely for a message if static, otherwise wait for frame duration */
        int rc = k_msgq_get(&led_message_queue, &msg, (current_state == STATIC && current_command == NONE && !dither_enabled) ? K_FOREVER : K_NO_WAIT);
        // int rc = k_msgq_get(&led_message_queue, &msg, K_NO_WAIT);
        
        if (rc == 0)
        {
            if (msg.command != NONE)
            {
                current_command = msg.command;
            }
            else
            {
                LOG_WRN("Received message with no command, ignoring");
                continue;
            }

            if (msg.params & LED_PARAM_BRIGHTNESS)
            {
                /* Temporarily disabled to allow for manual refresh */
                // if (msg.new_brightness == master_brightness)
                // {
                //     LOG_INF("Brightness unchanged at %d, skipping update", msg.new_brightness);
                //     continue; // No change in brightness, skip
                // }
                // LOG_INF("Updating master brightness to %d", msg.new_brightness);
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

            if (msg.params & LED_PARAM_DURATION)
            {
                // duration_ms = abs(msg.duration);
                duration_ms = msg.duration;
            }

            if (duration_ms <= FRAME_MS)
            {
                current_command = SET;
            }

            scale_brightness_16(&linear_rgbw_16, &base_rgb, master_brightness);
            gamma_correct_16(&gamma_rgbw_16, &linear_rgbw_16);
            // rgb2rgbw(&linear_rgbw_16, &new_color);

            start_color_16 = pre_dithered_color_16; // Set start color for animation to current color on strip
            start_time = k_uptime_get();
            frame_time = k_uptime_get();
            total_frame_time = 0;
            frames = 0;

            LOG_INF("Starting color: R=%d.%02d, G=%d.%02d, B=%d.%02d, W=%d.%02d",
                    rgbw_16_int(start_color_16.r_16), rgbw_16_frac(start_color_16.r_16),
                    rgbw_16_int(start_color_16.g_16), rgbw_16_frac(start_color_16.g_16),
                    rgbw_16_int(start_color_16.b_16), rgbw_16_frac(start_color_16.b_16),
                    rgbw_16_int(start_color_16.w_16), rgbw_16_frac(start_color_16.w_16));
            LOG_INF("Base color: R=%d, G=%d, B=%d, W=%d", base_rgb.r, base_rgb.g, base_rgb.b, base_rgb.w);
            LOG_INF("Corrected color: R=%d.%02d, G=%d.%02d, B=%d.%02d, W=%d.%02d, brightness: %d, duration: %ums",
                    rgbw_16_int(gamma_rgbw_16.r_16), rgbw_16_frac(gamma_rgbw_16.r_16),
                    rgbw_16_int(gamma_rgbw_16.g_16), rgbw_16_frac(gamma_rgbw_16.g_16),
                    rgbw_16_int(gamma_rgbw_16.b_16), rgbw_16_frac(gamma_rgbw_16.b_16),
                    rgbw_16_int(gamma_rgbw_16.w_16), rgbw_16_frac(gamma_rgbw_16.w_16),
                    master_brightness, duration_ms);

            if (current_command == FADE)
            {
                LOG_INF("Starting fade animation, duration: %lldms", duration_ms);
            }

        }

        if ((rgbw_16_int(gamma_rgbw_16.r_16) <= DITHER_THRESHOLD || rgbw_16_int(gamma_rgbw_16.g_16) <= DITHER_THRESHOLD ||
             rgbw_16_int(gamma_rgbw_16.b_16) <= DITHER_THRESHOLD || rgbw_16_int(gamma_rgbw_16.w_16) <= DITHER_THRESHOLD) 
             && dither_enabled)
        {
            current_state = DITHERING;
        }
        else
        {
            current_state = STATIC;
        }

        frame_time = k_uptime_get();

        switch (current_command)
        {
            case SET:
                memcpy(&pre_dithered_color_16, &gamma_rgbw_16, sizeof(led_rgbw_16));
                current_command = NONE;
                break;
            case FADE:
                elapsed_time = k_uptime_get() - start_time;
                // k_sleep(K_MSEC(50));

                if (elapsed_time >= duration_ms)
                {
                    elapsed_time = duration_ms; // Cap elapsed time to duration
                    pre_dithered_color_16 = gamma_rgbw_16; // Ensure final color is set at end of fade
                    current_command = NONE;
                    LOG_INF("Fade complete, duration: %lldms", elapsed_time);
                    // LOG_INF("Fade complete, start color: R=%d, G=%d, B=%d, W=%d", start_color.r, start_color.g, start_color.b, start_color.w);
                    // LOG_INF("Fade complete, original new color: R=%d, G=%d, B=%d, W=%d", new_color.r, new_color.g, new_color.b, new_color.w);
                    // LOG_INF("Fade complete, final color: R=%d, G=%d, B=%d, W=%d", current_color.r, current_color.g, current_color.b, current_color.w);
                    break;
                }

                if(frames % 10 == 0)
                {
                    LOG_INF("Fading... elapsed time: %lldms", elapsed_time);
                }
                pre_dithered_color_16.r_16 = start_color_16.r_16 + (((int)gamma_rgbw_16.r_16 - (int)start_color_16.r_16) * elapsed_time) / (int64_t)duration_ms;
                pre_dithered_color_16.g_16 = start_color_16.g_16 + (((int)gamma_rgbw_16.g_16 - (int)start_color_16.g_16) * elapsed_time) / (int64_t)duration_ms;
                pre_dithered_color_16.b_16 = start_color_16.b_16 + (((int)gamma_rgbw_16.b_16 - (int)start_color_16.b_16) * elapsed_time) / (int64_t)duration_ms;
                pre_dithered_color_16.w_16 = start_color_16.w_16 + (((int)gamma_rgbw_16.w_16 - (int)start_color_16.w_16) * elapsed_time) / (int64_t)duration_ms;

                // current_color.r = start_color.r + (((int)new_color.r - (int)start_color.r) * elapsed_time) / duration_ms;
                // current_color.g = start_color.g + (((int)new_color.g - (int)start_color.g) * elapsed_time) / duration_ms;
                // current_color.b = start_color.b + (((int)new_color.b - (int)start_color.b) * elapsed_time) / duration_ms;
                // current_color.w = start_color.w + (((int)new_color.w - (int)start_color.w) * elapsed_time) / duration_ms;


                // LOG_INF("Current color during fade: R=%d, G=%d, B=%d, W=%d, elapsed_time: %lldms", current_color.r, current_color.g, current_color.b, current_color.w, elapsed_time);
                break;
            default:
                break;
        }


        update_rgbw_strip();

        frames++;
        total_frame_time += k_uptime_get() - frame_time;
        if (frames % 1000 == 0)
        {
            LOG_INF("1000 average frame time: %lld.%03lldms", total_frame_time / 1000, (total_frame_time % 1000));
            frames = 0;
            total_frame_time = 0;
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

static inline void scale_brightness_16(led_rgbw_16 *scaled_color, const led_rgbw *color, const uint8_t brightness)
{
    if (brightness == 255)
    {
        LOG_INF("Brightness at 255, skipping scaling");
        scaled_color->r_16 = color->r << 8;
        scaled_color->g_16 = color->g << 8;
        scaled_color->b_16 = color->b << 8;
        scaled_color->w_16 = color->w << 8;
        return; // No scaling needed, just shift to 16-bit range
    }
    else if (brightness == 0)
    {
        LOG_INF("Brightness at 0, setting all channels to 0");
        scaled_color->r_16 = 0;
        scaled_color->g_16 = 0;
        scaled_color->b_16 = 0;
        scaled_color->w_16 = 0;
        return; // All channels off
    }

    scaled_color->r_16 = (color->r * brightness);
    scaled_color->g_16 = (color->g * brightness);
    scaled_color->b_16 = (color->b * brightness);
    scaled_color->w_16 = (color->w * brightness);
}

static inline void gamma_correct_16(led_rgbw_16 *corrected_color, const led_rgbw_16 *color)
{
    corrected_color->r_16 = (gamma_table[color->r_16 >> 8]);
    corrected_color->g_16 = (gamma_table[color->g_16 >> 8]);
    corrected_color->b_16 = (gamma_table[color->b_16 >> 8]);
    corrected_color->w_16 = (gamma_table[color->w_16 >> 8]);
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

static void update_rgbw_strip()
{
    static led_rgbw dithered_color; // Color after dithering (if enabled); goes directly to strip
    static led_rgbw pixels[STRIP_NUM_PIXELS];
    static uint8_t dither_accum_r, dither_accum_g, dither_accum_b, dither_accum_w;
	int rc;
	// memset(pixels, 0x00, sizeof(pixels));

    if (rgbw_16_int(pre_dithered_color_16.r_16) < DITHER_THRESHOLD && current_state == DITHERING && dither_enabled)
    {
        dithered_color.r = rgbw_16_int(pre_dithered_color_16.r_16) +
                           ((dither_accum_r += rgbw_16_frac(pre_dithered_color_16.r_16)) < rgbw_16_frac(pre_dithered_color_16.r_16));
    }
    else
    {
        dithered_color.r = rgbw_16_int(pre_dithered_color_16.r_16);
    }

    if (rgbw_16_int(pre_dithered_color_16.g_16) < DITHER_THRESHOLD && current_state == DITHERING && dither_enabled)
    {
        dithered_color.g = rgbw_16_int(pre_dithered_color_16.g_16) +
                            ((dither_accum_g += rgbw_16_frac(pre_dithered_color_16.g_16)) < rgbw_16_frac(pre_dithered_color_16.g_16));
    }
    else
    {
        dithered_color.g = rgbw_16_int(pre_dithered_color_16.g_16);
    }

    if (rgbw_16_int(pre_dithered_color_16.b_16) < DITHER_THRESHOLD && current_state == DITHERING && dither_enabled)
    {
        dithered_color.b = rgbw_16_int(pre_dithered_color_16.b_16) +
                            ((dither_accum_b += rgbw_16_frac(pre_dithered_color_16.b_16)) < rgbw_16_frac(pre_dithered_color_16.b_16));
    }
    else
    {
        dithered_color.b = rgbw_16_int(pre_dithered_color_16.b_16);
    }

    if (rgbw_16_int(pre_dithered_color_16.w_16) < DITHER_THRESHOLD && current_state == DITHERING && dither_enabled)
    {
        dithered_color.w = rgbw_16_int(pre_dithered_color_16.w_16) +
                            ((dither_accum_w += rgbw_16_frac(pre_dithered_color_16.w_16)) < rgbw_16_frac(pre_dithered_color_16.w_16));
    }
    else
    {
        dithered_color.w = rgbw_16_int(pre_dithered_color_16.w_16);
    }

    // if (current_state == DITHERING && dither_enabled)
    // {
    //     LOG_INF("Dithering applied, pre-dither color: R=%d.%02d, G=%d.%02d, B=%d.%02d, W=%d.%02d",
    //             rgbw_16_int(pre_dithered_color_16.r_16), rgbw_16_frac(pre_dithered_color_16.r_16),
    //             rgbw_16_int(pre_dithered_color_16.g_16), rgbw_16_frac(pre_dithered_color_16.g_16),
    //             rgbw_16_int(pre_dithered_color_16.b_16), rgbw_16_frac(pre_dithered_color_16.b_16),
    //             rgbw_16_int(pre_dithered_color_16.w_16), rgbw_16_frac(pre_dithered_color_16.w_16));
    //     LOG_INF("Dithering applied, accumulated error: R=%d, G=%d, B=%d, W=%d", dither_accum_r, dither_accum_g, dither_accum_b, dither_accum_w);
    // }
	// for (size_t i = 0; i < 4; i++)
    // {
    //     if (current_state == DITHERING && dither_enabled)
    //     {
    //         memcpy(&pixels[i], &final_color, sizeof(led_rgbw));
    //     }
    //     else
    //     {
    //         memcpy(&pixels[i], &current_color, sizeof(led_rgbw));
    //     }
	// }
    for (size_t i = 0; i < 4; i++)
    {
        memcpy(&pixels[i], &dithered_color, sizeof(led_rgbw));
    }    
    if (frames % 100 == 0 && current_state == DITHERING && dither_enabled)
    {
        // LOG_INF("Pre-dither color: R=%d.%02d, G=%d.%02d, B=%d.%02d, W=%d.%02d",
        //         rgbw_16_int(pre_dithered_color_16.r_16), rgbw_16_frac(pre_dithered_color_16.r_16),
        //         rgbw_16_int(pre_dithered_color_16.g_16), rgbw_16_frac(pre_dithered_color_16.g_16),
        //         rgbw_16_int(pre_dithered_color_16.b_16), rgbw_16_frac(pre_dithered_color_16.b_16),
        //         rgbw_16_int(pre_dithered_color_16.w_16), rgbw_16_frac(pre_dithered_color_16.w_16));
        LOG_INF("Output color: R=%d, G=%d, B=%d, W=%d", dithered_color.r, dithered_color.g, dithered_color.b, dithered_color.w);
    }

	rc = rgbw_strip_update_rgbw(strip, pixels, STRIP_NUM_PIXELS);

	if (rc)
	{
		LOG_ERR("Couldn't update strip: %d", rc);
	}
}
