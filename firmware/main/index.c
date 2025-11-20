#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include "scan.h"

void app_main() {
	ESP_LOGI("TEST", "Hello from C!");

	scannerBegin();

	while (true) {
		if (scanReady == true) {
			printf("%d: <", scanIndex);
			printf(scan);
			printf(">\n");

			scanReady = false;
		}

		vTaskDelay(1);
	}
}
