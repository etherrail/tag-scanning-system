#include "esp_log.h"
#include <stdio.h>

extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/task.h"
	#include "esp_system.h"

	void app_main();
}

#include "scan.cpp"
#include "display.cpp"

void app_main(void) {
	ESP_LOGI("MAIN", "start");

	scannerBegin();

	Display display;
	display.begin();

	while (true) {
		if (scanReady == true) {
			printf("%d: <", scanIndex);
			printf(scan);
			printf(">\n");

			display.presentTag(scan);

			scanReady = false;
		}

		vTaskDelay(1);
	}
}
