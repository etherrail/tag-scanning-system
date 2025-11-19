#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"

extern "C" void app_main();

void app_main() {
	ESP_LOGI("TEST", "Hello from C++!");
}
