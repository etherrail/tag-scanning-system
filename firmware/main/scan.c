#include "scan.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "errno.h"
#include "driver/gpio.h"

#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/hid_usage_mouse.h"

static const char *TAG = "SCAN";

char scanBuffer[MAX_SCAN_LENGTH];
int scanIndex = 0;

char scan[MAX_SCAN_LENGTH];
bool scanReady = false;

typedef enum {
	APP_EVENT = 0,
	APP_EVENT_HID_HOST
} app_event_group_t;


/**
 * @brief HID Protocol string names
 */
static const char *hid_proto_name_str[] = {
	"NONE",
	"KEYBOARD",
	"MOUSE"
};

typedef struct {
	app_event_group_t event_group;
	/* HID Host - Device related info */
	struct {
		hid_host_device_handle_t handle;
		hid_host_driver_event_t event;
		void *arg;
	} hid_host_device;
} app_event_queue_t;

typedef enum {
	KEY_STATE_PRESSED = 0x00,
	KEY_STATE_RELEASED = 0x01
} key_state_t;

typedef struct {
	key_state_t state;
	uint8_t modifier;
	uint8_t key_code;
} key_event_t;

static void key_event_callback(key_event_t *key_event) {
	ESP_LOGI(tag, "hit %x", key_event->key_code);

	if (key_event->state == KEY_STATE_PRESSED) {
		return;
	}

	if (scanReady) {
		return;
	}

	// end read character
	//
	// enter / tab / space
	if (key_event->key_code == 40 || key_event->key_code == 43 || key_event->key_code == 44) {
		memcpy(scan, scanBuffer, scanIndex);
		scan[scanIndex] = '\0';

		scanReady = true;
		scanIndex = 0;
	}

	unsigned char character = 0;

	// zero
	if (key_event->key_code == 39) {
		character = '0';
	}

	// numbers
	if (key_event->key_code >= 30 && key_event->key_code <= 38) {
		character = '1' + (key_event->key_code - 30);
	}

	// letters
	char letter = key_event->key_code - 4 + 'a';

	if (letter >= 'a' && letter <= 'z') {
		character = letter;
	}

	if (character != 0) {
		scanBuffer[scanIndex++] = character;

		if (scanIndex == MAX_SCAN_LENGTH) {
			scanIndex = 0;
		}
	}
}

static inline bool key_found(
	const uint8_t *const src,
	uint8_t key,
	unsigned int length
) {
	for (unsigned int i = 0; i < length; i++) {
		if (src[i] == key) {
			return true;
		}
	}

	return false;
}

static void hid_host_keyboard_report_callback(const uint8_t *const data, const int length) {
	hid_keyboard_input_report_boot_t *kb_report = (hid_keyboard_input_report_boot_t *)data;

	if (length < sizeof(hid_keyboard_input_report_boot_t)) {
		return;
	}

	static uint8_t prev_keys[HID_KEYBOARD_KEY_MAX] = { 0 };
	key_event_t key_event;

	for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
		// key has been released verification
		if (
			prev_keys[i] > HID_KEY_ERROR_UNDEFINED &&
			!key_found(kb_report->key, prev_keys[i], HID_KEYBOARD_KEY_MAX)
		) {
			key_event.key_code = prev_keys[i];
			key_event.modifier = 0;
			key_event.state = KEY_STATE_RELEASED;

			key_event_callback(&key_event);
		}

		// key has been pressed verification
		if (
			kb_report->key[i] > HID_KEY_ERROR_UNDEFINED &&
			!key_found(prev_keys, kb_report->key[i], HID_KEYBOARD_KEY_MAX)
		) {
			key_event.key_code = kb_report->key[i];
			key_event.modifier = kb_report->modifier.val;
			key_event.state = KEY_STATE_PRESSED;
			key_event_callback(&key_event);
		}
	}

	memcpy(prev_keys, &kb_report->key, HID_KEYBOARD_KEY_MAX);
}

void hid_host_interface_callback(
	hid_host_device_handle_t hid_device_handle,
	const hid_host_interface_event_t event,
	void *arg
) {
	uint8_t data[64] = { 0 };
	size_t data_length = 0;
	hid_host_dev_params_t dev_params;
	ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

	switch (event) {
		case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
			ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(
				hid_device_handle,
				data,
				64,
				&data_length
			));

			if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) {
				if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
					hid_host_keyboard_report_callback(data, data_length);
				}
			}

			break;
		}

		case HID_HOST_INTERFACE_EVENT_DISCONNECTED: {
			ESP_LOGI(TAG, "device disconnected");
			ESP_ERROR_CHECK(hid_host_device_close(hid_device_handle));

			break;
		}

		case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR: {
			ESP_LOGI(TAG, "device transfer error");

			break;
		}

		default: {
			ESP_LOGE(TAG, "device unknown error");

			break;
		}
	}
}

static void usb_lib_task(void *arg) {
	usb_host_config_t host_config = {};
	host_config.skip_phy_setup = false;
	host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;

	ESP_ERROR_CHECK(usb_host_install(&host_config));
	xTaskNotifyGive(arg);

	while (true) {
		uint32_t event_flags;
		usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

		if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
			ESP_ERROR_CHECK(usb_host_device_free_all());
			break;
		}
	}

	ESP_LOGI(TAG, "USB shutdown");
	vTaskDelay(10); // Short delay to allow clients clean-up
	ESP_ERROR_CHECK(usb_host_uninstall());
	vTaskDelete(NULL);
}


QueueHandle_t app_event_queue = NULL;

void hid_host_device_callback(
	hid_host_device_handle_t hid_device_handle,
	const hid_host_driver_event_t event,
	void *arg
) {
	app_event_queue_t evt_queue = {};
	evt_queue.event_group = APP_EVENT_HID_HOST;

	// HID Host Device related info
	evt_queue.hid_host_device.handle = hid_device_handle;
	evt_queue.hid_host_device.event = event;
	evt_queue.hid_host_device.arg = arg;

	if (app_event_queue) {
		xQueueSend(app_event_queue, &evt_queue, 0);
	}
}

void hid_host_device_event(
	hid_host_device_handle_t hid_device_handle,
	const hid_host_driver_event_t event,
	void *arg
) {
	hid_host_dev_params_t dev_params;
	ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

	if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
		ESP_LOGI(TAG, "device connected");

		const hid_host_device_config_t dev_config = {
			.callback = hid_host_interface_callback,
			.callback_arg = NULL
		};

		ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));

		if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) {
			ESP_ERROR_CHECK(hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT));

			if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
				ESP_ERROR_CHECK(hid_class_request_set_idle(hid_device_handle, 0, 0));
			}
		}

		ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
	}
}

void scannerTask(void *args) {
	BaseType_t task_created;
	app_event_queue_t evt_queue;

	task_created = xTaskCreatePinnedToCore(
		usb_lib_task,
		"usb_events",
		4096,
		xTaskGetCurrentTaskHandle(),
		2, NULL, 0
	);

	assert(task_created == pdTRUE);

	// Wait for notification from usb_lib_task to proceed
	ulTaskNotifyTake(false, 1000);

	hid_host_driver_config_t hid_host_driver_config = {};  // zero-initialize
	hid_host_driver_config.create_background_task = true;
	hid_host_driver_config.task_priority = 5;
	hid_host_driver_config.stack_size = 4096;
	hid_host_driver_config.core_id = 0;
	hid_host_driver_config.callback = hid_host_device_callback;
	hid_host_driver_config.callback_arg = NULL;

	ESP_ERROR_CHECK(hid_host_install(&hid_host_driver_config));

	app_event_queue = xQueueCreate(10, sizeof(app_event_queue_t));

	while (1) {
		// Wait queue
		if (xQueueReceive(app_event_queue, &evt_queue, portMAX_DELAY)) {
			if (APP_EVENT_HID_HOST == evt_queue.event_group) {
				hid_host_device_event(
					evt_queue.hid_host_device.handle,
					evt_queue.hid_host_device.event,
					evt_queue.hid_host_device.arg
				);
			}
		}
	}
}

void scannerBegin() {
	xTaskCreate(scannerTask, "scanner", 4096, NULL, 0, NULL);
}
