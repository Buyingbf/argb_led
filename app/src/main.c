#include <errno.h>
#include <string.h>

#define LOG_LEVEL 4
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#include <rgbw_strip.h>				// RGBW strip driver
// #include <zephyr/drivers/led_strip.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/util.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>	// BLE connections
#include <zephyr/random/random.h>
#include <zephyr/debug/coredump.h>

#include <bluetooth/services/lbs.h> // LED button service


#include <dk_buttons_and_leds.h>
#include "led_strip_service.h"
#include "led_anim_thread.h"

#define USER_BUTTON				DK_BTN1_MSK
#define CONNECTION_STATUS_LED   DK_LED2

#define DELAY_TIME K_MSEC(1000)

struct bt_conn *my_conn = NULL;
static struct k_work adv_work;

extern struct k_msgq led_message_queue; 

extern bool no_white_component;

static const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
	(BT_LE_ADV_OPT_CONN |
	 BT_LE_ADV_OPT_USE_IDENTITY), /* Connectable advertising and use identity address */
	BT_GAP_ADV_FAST_INT_MIN_1, /* 0x30 units, 48 units, 30ms */
	BT_GAP_ADV_FAST_INT_MAX_1, /* 0x60 units, 96 units, 60ms */
	NULL); /* Set to NULL for undirected advertising */

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
			  BT_UUID_128_ENCODE(0x00001523, 0x1212, 0xefde, 0x1523, 0x785feabcd123)),
};

static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Advertising successfully started");
}

static void advertising_start(void)
{
	k_work_submit(&adv_work);
}

static void set_color_rgbw(const led_rgbw *new_color)
{
	k_msgq_put(&led_message_queue, &(struct led_msg){
		.new_rgbw = *new_color,
		.command = SET,
	}, K_FOREVER);
}

static void set_brightness(const uint8_t *new_brightness)
{
	k_msgq_put(&led_message_queue, &(struct led_msg){
		.new_brightness = *new_brightness,
		.command = SET,
	}, K_FOREVER);
	
}

static void update_phy(struct bt_conn *conn)
{
    int err;
    const struct bt_conn_le_phy_param preferred_phy = {
        .options = BT_CONN_LE_PHY_OPT_NONE,
        .pref_rx_phy = BT_GAP_LE_PHY_2M,
        .pref_tx_phy = BT_GAP_LE_PHY_2M,
    };
    err = bt_conn_le_phy_update(conn, &preferred_phy);
    if (err) {
        LOG_ERR("bt_conn_le_phy_update() returned %d", err);
    }
}

void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection error %d", err);
		return;
	}

	LOG_INF("Connected");
	my_conn = bt_conn_ref(conn);

	dk_set_led(CONNECTION_STATUS_LED, 1);

	struct bt_conn_info info;
	err = bt_conn_get_info(conn, &info);
	if (err) {
		LOG_ERR("bt_conn_get_info() returned %d", err);
		return;
	}

	double connection_interval = BT_CONN_INTERVAL_TO_MS(info.le.interval); // in ms
	uint16_t supervision_timeout = info.le.timeout*10; // in ms
	LOG_INF("Connection parameters: interval %.2f ms, latency %d intervals, timeout %d ms", connection_interval, info.le.latency, supervision_timeout);
	
	update_phy(my_conn);
}

void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected. Reason %d", reason);
	bt_conn_unref(my_conn);

	/* STEP 3.3  Turn the connection status LED off */
	dk_set_led(CONNECTION_STATUS_LED, 0);
}

void on_recycled(void)
{
	advertising_start();
}

void on_le_param_updated(struct bt_conn *conn, uint16_t interval,
				 uint16_t latency, uint16_t timeout)
{
    double connection_interval = interval*1.25;         // in ms
    uint16_t supervision_timeout = timeout*10;          // in ms
    LOG_INF("Connection parameters updated: interval %.2f ms, latency %d intervals, timeout %d ms", connection_interval, latency, supervision_timeout);
}

void on_le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
    // PHY Updated
    if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_1M) {
        LOG_INF("PHY updated. New PHY: 1M");
    }
    else if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_2M) {
        LOG_INF("PHY updated. New PHY: 2M");
    }
    else if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_CODED_S8) {
        LOG_INF("PHY updated. New PHY: Long Range");
    }
}

/* BLE connection callbacks*/
struct bt_conn_cb connection_callbacks = {
    .connected              = on_connected,
    .disconnected           = on_disconnected,
    .recycled               = on_recycled,
	.le_param_updated       = on_le_param_updated,
	.le_phy_updated         = on_le_phy_updated,
};

struct my_lss_cb lss_callbacks = {
	.color_cb = set_color_rgbw,
	.brightness_cb = set_brightness,
};

/* Callback handler for when button is pressed*/
static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	int err;
	bool user_button_changed = (has_changed & USER_BUTTON) ? true : false;
	bool user_button_pressed = (button_state & USER_BUTTON) ? true : false;
	if (user_button_changed) {
		LOG_INF("Button %s", (user_button_pressed ? "pressed" : "released"));
		if (user_button_pressed)
		{
			k_msgq_put(&led_message_queue, &(struct led_msg){
				.new_rgbw = (led_rgbw){
					.r = sys_rand8_get(),
					.g = sys_rand8_get(),
					.b = sys_rand8_get(),
					.w = sys_rand8_get(),
				},
				.command = FADE,
				.params = LED_PARAM_COLOR | LED_PARAM_DURATION,
				.duration = 1000
			}, K_NO_WAIT);
		}		
		err = bt_lbs_send_button_state(user_button_pressed);
		if (err) {
			LOG_ERR("Couldn't send notification. (err: %d)", err);
		}
	}

	if (button_state & DK_BTN2_MSK) // If button state changed and button is now released, toggle white component on/off
	{
		no_white_component = !no_white_component; // Toggle white component on/off with second button
		k_msgq_put(&led_message_queue, &(struct led_msg){
			.new_brightness = 0x7f,
			.params = LED_PARAM_BRIGHTNESS,
			.command = SET,
		}, K_FOREVER);
	}
}

static int init_button(void)
{
	int err = 0;
	err = dk_buttons_init(button_changed);
	if (err) {
		LOG_ERR("Cannot init buttons (err: %d)", err);
	}

	return err;
}

int main()
{
	int rc;
	int err;

	////
	// Initializers
	////
	
	/* Initialize DK LEDs*/
	err = dk_leds_init();
	if (err)
	{
		LOG_ERR("LEDs init failed (err %d)", err);
		return -1;
	}

	/* Initialize DK buttons */
	err = init_button();
	if (err)
	{
		LOG_ERR("Button init failed (err %d)", err);
	}

	/* Register BLE connection callbacks */
	err = bt_conn_cb_register(&connection_callbacks);
	if (err) {
		LOG_ERR("Connection callback register failed (err %d)", err);
	}

	/* Enable BT */
	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return -1;
	}
	LOG_INF("Bluetooth initialized");

	/* Initialize the LED Strip Service */
	err = my_lss_init(&lss_callbacks);
	if (err) {
		LOG_ERR("Failed to initialize LSS (err %d)", err);
	}

	/* Assign advertising start to a work unit*/
	k_work_init(&adv_work, adv_work_handler); // Init work structure prior to submitting work
	advertising_start();

	// k_work_init(&led_work, led_work_handler);
	// LOG_INF("Displaying pattern on strip");

	// color_index = 0;
	// cursor = 0;
	// brightness = 0x3f;
	// color = (led_rgbw) {
	// 	.r = 0x00,
	// 	.g = 0x00,
	// 	.b = 0x00,
	// 	.w = 0xff
	// };
	// update_rgbw_strip();


	while (1) {
		dk_set_led(DK_LED1, 1);
		k_sleep(DELAY_TIME);
		dk_set_led(DK_LED1, 0);
		k_sleep(DELAY_TIME);
		
	}

    return 0;
}

K_THREAD_DEFINE(led_anim_thread_id, 1024, led_anim_thread, NULL, NULL, NULL, 1, 0, 0);