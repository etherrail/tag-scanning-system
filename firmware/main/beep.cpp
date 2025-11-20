#include "beep.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"

#include "es8311.h"

#include "driver/i2c_master.h"

static const char *TAG = "Beep";

const int AUDIO_SAMPLE_RATE = 16000;
const i2s_mclk_multiple_t AUDIO_MCLK_MULTIPLE = I2S_MCLK_MULTIPLE_256;
const int AUDIO_MCLK_FREQUENCY = (AUDIO_SAMPLE_RATE * AUDIO_MCLK_MULTIPLE);

const i2c_port_t CODEC_PORT = I2C_NUM_0;
const gpio_num_t CODEC_SDA_GPIO = GPIO_NUM_7;
const gpio_num_t CODEC_SCL_GPIO = GPIO_NUM_8;
const int CODEC_SPEED_HZ = 400000;

const gpio_num_t SPEAKER_I2S_MCLK_GPIO = GPIO_NUM_13;
const gpio_num_t SPEAKER_I2S_BCLK_GPIO = GPIO_NUM_12;
const gpio_num_t SPEAKER_I2S_WS_GPIO = GPIO_NUM_10;
const gpio_num_t SPEAKER_I2S_DOUT_GPIO = GPIO_NUM_11;
const gpio_num_t SPEAKER_I2S_DIN_GPIO = I2S_GPIO_UNUSED;

const gpio_num_t AMPLIFIER_ENABLE_GPIO = GPIO_NUM_53;

const int AUDIO_BUFFER_FRAME_COUNT = 240; // frames per DMA buffer
const int AUDIO_BUFFER_DESC_COUNT = 8;

typedef struct {
	int frequency;
	int duration;
} Tone;

static i2s_chan_handle_t audioWriteChannel = NULL;

static i2c_master_bus_handle_t codec_bus;

static esp_err_t prepareCodec() {
	i2c_master_bus_config_t bus_cfg = {};
	bus_cfg.i2c_port = CODEC_PORT;
	bus_cfg.sda_io_num = CODEC_SDA_GPIO;
	bus_cfg.scl_io_num = CODEC_SCL_GPIO;
	bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
	bus_cfg.glitch_ignore_cnt = 7;
	bus_cfg.intr_priority = 0;
	bus_cfg.flags.enable_internal_pullup = false;1

	ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &codec_bus));

	for (uint8_t addr = 0x18; addr < 0x7f; addr++) {
		ESP_LOGI("I2C", "ADDR %d", addr);

		esp_err_t err = i2c_master_probe(codec_bus, addr, 10);

		if (err == ESP_OK) {
			ESP_LOGI("I2C", "YES");
		} else {
			ESP_LOGI("I2C", "NO");
		}
	}

	return ESP_OK;
}

static esp_err_t startCodec() {
	ESP_LOGI("CODEC", "CREATE HANDLE");

	// create ES8311 handle on our I2C bus
	es8311_handle_t es = es8311_create(CODEC_PORT, ES8311_ADDRRES_0);
	ESP_RETURN_ON_FALSE(es, ESP_FAIL, TAG, "es8311 create failed");

	const es8311_clock_config_t clk_cfg = {
		.mclk_inverted = false,
		.sclk_inverted = false,
		.mclk_from_mclk_pin = true,
		.mclk_frequency = AUDIO_MCLK_FREQUENCY,
		.sample_frequency = AUDIO_SAMPLE_RATE,
	};

	ESP_LOGI("CODEC", "INIT");

	ESP_RETURN_ON_ERROR(es8311_init(
		es,
		&clk_cfg,
		ES8311_RESOLUTION_16,
		ES8311_RESOLUTION_16
	), TAG, "es8311 init failed");

	ESP_LOGI("CODEC", "SAMPLE CONFIG");

	ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(
		es,
		AUDIO_SAMPLE_RATE * AUDIO_MCLK_MULTIPLE,
		AUDIO_SAMPLE_RATE
	), TAG, "es8311 sample freq failed");

	ESP_LOGI("CODEC", "VOLUME");

	ESP_RETURN_ON_ERROR(es8311_voice_volume_set(
		es,
		100, // volume 0 - 100
		NULL
	), TAG, "es8311 volume failed");

	ESP_LOGI("CODEC", "DISABLE MICROPHONE");

	// playback only (no mic echo)
	ESP_RETURN_ON_ERROR(es8311_microphone_config(
		es,
		false
	), TAG, "es8311 mic cfg failed");

	return ESP_OK;
}

static esp_err_t prepareAudioInterface() {
	i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
	chan_cfg.dma_desc_num  = AUDIO_BUFFER_DESC_COUNT;
	chan_cfg.dma_frame_num = AUDIO_BUFFER_FRAME_COUNT;
	chan_cfg.auto_clear = true;

	ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &audioWriteChannel, NULL), TAG, "new channel failed");

	i2s_std_config_t std_cfg = {
		.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
		.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
			I2S_DATA_BIT_WIDTH_16BIT,
			I2S_SLOT_MODE_STEREO
		),
		.gpio_cfg = {
			.mclk = SPEAKER_I2S_MCLK_GPIO,
			.bclk = SPEAKER_I2S_BCLK_GPIO,
			.ws = SPEAKER_I2S_WS_GPIO,
			.dout = SPEAKER_I2S_DOUT_GPIO,
			.din = SPEAKER_I2S_DIN_GPIO,
			.invert_flags = {
				.mclk_inv = false,
				.bclk_inv = false,
				.ws_inv = false,
			},
		},
	};

	// make sure MCLK multiple matches codec config
	std_cfg.clk_cfg.mclk_multiple = AUDIO_MCLK_MULTIPLE;

	ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(audioWriteChannel, &std_cfg), TAG, "i2s init std failed");
	ESP_RETURN_ON_ERROR(i2s_channel_enable(audioWriteChannel), TAG, "i2s enable failed");

	return ESP_OK;
}

static void enableAmplifier(bool enable) {
	gpio_reset_pin(AMPLIFIER_ENABLE_GPIO);
	gpio_set_direction(AMPLIFIER_ENABLE_GPIO, GPIO_MODE_OUTPUT);

	gpio_set_level(AMPLIFIER_ENABLE_GPIO, enable ? 1 : 0);
}

static void playSoundTask(void *argument) {
	Tone *tone = static_cast<Tone *>(argument);

	ESP_LOGI("BEEP", "BEEPED %d %d", tone->duration, tone->frequency);

	const int16_t amplitude = 20000;

	int totalSamples = (AUDIO_SAMPLE_RATE * tone->duration) / 1000;
	int samplesWritten = 0;

	const int samplesPerCycle = AUDIO_SAMPLE_RATE / tone->frequency;
	int16_t *audioBuffer = new int16_t[samplesPerCycle * 2]();

	for (int n = 0; n < samplesPerCycle; ++n) {
		float phase = 2.0f * M_PI * (float)n / (float)samplesPerCycle;
		int16_t sample = (int16_t)(amplitude * sinf(phase));

		// stereo: L,R interleaved
		audioBuffer[2 * n + 0] = sample;
		audioBuffer[2 * n + 1] = sample;

		ESP_LOGI("****", "%d %d", n, sample);
	}

	ESP_LOGI(TAG, "Starting tone...");

	while (samplesWritten < totalSamples) {
		size_t written = 0;

		esp_err_t err = i2s_channel_write(
			audioWriteChannel,
			audioBuffer,
			samplesPerCycle * 2 * sizeof(int16_t),
			&written,
			portMAX_DELAY
		);

		samplesWritten += (written / (2 * sizeof(int16_t)));

		if (err != ESP_OK) {
			ESP_LOGE(TAG, "i2s write error: %s", esp_err_to_name(err));
		}
	}

	delete tone;
	delete[] audioBuffer;

	vTaskDelete(nullptr);
}

Beep::Beep() {
	ESP_LOGI("BEEP", "PREPARE CODEC");
	ESP_ERROR_CHECK(prepareCodec());

	return;

	ESP_LOGI("BEEP", "PREPARE AUDIO INTERFACE");
	ESP_ERROR_CHECK(prepareAudioInterface());

	ESP_LOGI("BEEP", "START CODEC");
	ESP_ERROR_CHECK(startCodec());

	ESP_LOGI("BEEP", "START AMPLIFIER");
	enableAmplifier(true);

	ESP_LOGI("BEEP", "PREPARED");
}

void Beep::success() {
	play(1000, 440);
}

void Beep::play(int duration, int frequency) {
	ESP_LOGI("BEEP", "BEEPED %d %d", duration, frequency);

	Tone *tone = new Tone {
		.frequency = frequency,
		.duration = duration
	};

	xTaskCreate(playSoundTask, "beep", 4096, tone, 5, NULL);
}
