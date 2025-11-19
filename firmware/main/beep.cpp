#include "beep.h"

#include "esp_log.h"

void Beep::success() {
	play(250, 440);
}

void Beep::play(int length, int frequency) {
	ESP_LOGI("BEEP", "BEEPED %d %d", length, frequency);
}
