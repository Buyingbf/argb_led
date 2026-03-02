#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <rgbw_strip.h>

#include "led_strip_service.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(led_strip_service);

static struct led_rgbw color;
static uint8_t brightness;
static struct my_lss_cb lss_cb;

static ssize_t write_color(struct bt_conn *conn,
					     const struct bt_gatt_attr *attr,
					     const void *buf,
                         uint16_t len,
					     uint16_t offset,
                         uint8_t flags)
{
	LOG_DBG("Attribute write color, handle: %u, conn: %p", attr->handle, (void *)conn);
    LOG_INF("Received color values: R=%u, G=%u, B=%u, W=%u",
        ((struct led_rgbw*)buf)->r,
        ((struct led_rgbw*)buf)->g,
        ((struct led_rgbw*)buf)->b,
        ((struct led_rgbw*)buf)->w);


	if (len != 4U) {
		LOG_ERR("Write color: Incorrect data length");
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	if (offset != 0) {
		LOG_ERR("Write color: Incorrect data offset");
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (lss_cb.color_cb) {
		// Read the received value
		struct led_rgbw color;
        color.r = (uint8_t) ((*(uint32_t*)(buf)) >> 24) & 0xFF;
        color.g = (uint8_t) ((*(uint32_t*)(buf)) >> 16) & 0xFF;
        color.b = (uint8_t) ((*(uint32_t*)(buf)) >> 8)  & 0xFF;
        color.w = (uint8_t) ((*(uint32_t*)(buf)) >> 0)  & 0xFF;

        lss_cb.color_cb(&color);
	}

	return len;
}

static ssize_t write_brightness(struct bt_conn *conn,
					     const struct bt_gatt_attr *attr,
					     const void *buf,
                         uint16_t len,
					     uint16_t offset,
                         uint8_t flags)
{
	LOG_DBG("Attribute write brightness, handle: %u, conn: %p", attr->handle, (void *)conn);
    LOG_INF("Received brightness value: %u", *(uint8_t*)buf);
    
	if (len != 1U) {
		LOG_ERR("Write brightness: Incorrect data length");
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	if (offset != 0) {
		LOG_ERR("Write brightness: Incorrect data offset");
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (lss_cb.brightness_cb) {
		// Read the received value
        uint8_t brightness = *(uint8_t*)(buf) & 0xFF;
        lss_cb.brightness_cb(&brightness);
	}

	return len;
}

BT_GATT_SERVICE_DEFINE(led_strip_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_LSS),
    BT_GATT_CHARACTERISTIC( BT_UUID_LSS_COLOR,
                            BT_GATT_CHRC_WRITE,
                            BT_GATT_PERM_WRITE,
                            NULL,
                            write_color,
                            &color),
    BT_GATT_CHARACTERISTIC( BT_UUID_LSS_BRIGHTNESS,
                            BT_GATT_CHRC_WRITE,
                            BT_GATT_PERM_WRITE, 
                            NULL, 
                            write_brightness, 
                            &brightness),
);

/* A function to register application callbacks for the color and brightness characteristics  */
int my_lss_init(struct my_lss_cb *callbacks)
{
	if (callbacks) {
		lss_cb.color_cb = callbacks->color_cb;
		lss_cb.brightness_cb = callbacks->brightness_cb;
	}
	return 0;
}

void brightness_scale_color(struct led_rgbw *scaled_color, struct led_rgbw *color, uint8_t brightness)
{
    scaled_color->r = (color->r * brightness) >> 8;
    scaled_color->g = (color->g * brightness) >> 8;
    scaled_color->b = (color->b * brightness) >> 8;
    scaled_color->w = (color->w * brightness) >> 8;
}