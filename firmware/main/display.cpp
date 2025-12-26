#include <ratio>
#include <stdio.h>
#include <string.h>

extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/task.h"

	#include "driver/gpio.h"

	#include "esp_check.h"
	#include "esp_heap_caps.h"
	#include "esp_log.h"

	#include "esp_lcd_panel_ops.h"
	#include "esp_lcd_panel_io.h"
	#include "esp_lcd_io_spi.h"

	#include "esp_lcd_st7796.h"
}

#define TAG "DISPLAY"

#define PIN_NUM_CLK GPIO_NUM_7
#define PIN_NUM_MOSI GPIO_NUM_8
#define PIN_NUM_DC GPIO_NUM_23
#define PIN_NUM_RST GPIO_NUM_5
#define PIN_NUM_CS GPIO_NUM_20

#define SPI_HOST SPI2_HOST

#define LCD_HEIGHT 320
#define LCD_WIDTH 480

#include "font/mono-40.cpp"

typedef struct Frame {
	const uint16_t x;
    const uint16_t y;

    const uint16_t width;
    const uint16_t height;

    const uint16_t color;

	uint16_t *canvas;

	Frame(
		uint16_t x, uint16_t y,
		uint16_t width, uint16_t height,
		uint16_t color
	) : x(x), y(y), width(width), height(height), color(color) {}
} Frame;

static const Glyph *findGlyph(const Font *font, char character) {
	for (size_t index = 0; index < font->glyphCount; index++) {
		if (font->glyphs[index].character == character) {
			return &font->glyphs[index];
		}
	}

	return NULL;
}

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
	return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint8_t drawCharacter(
	const Font *font,
	const Glyph *glyph,

	uint16_t *canvas,
	int canvasWidth,

	int x,
	int y,

	uint16_t fg,
	uint16_t bg
) {
	if (glyph == NULL) {
		for (int row = 0; row < font->height; row++) {
			for (int column = 0; column < font->height; column++) {
				canvas[(y + row) * canvasWidth + x + column] = fg;
			}
		}

		return font->height;
	}

	bool state = false;
	uint8_t row = 0;
	uint8_t column = 0;

	for (uint8_t segmentIndex = 0; segmentIndex < glyph->segmentCount; segmentIndex++) {
		for (uint16_t index = 0; index < glyph->segments[segmentIndex]; index++) {
			canvas[(y + row) * canvasWidth + x + column] = state ? fg : bg;

			column++;

			if (column == glyph->width) {
				column = 0;
				row++;
			}
		}

		state = !state;
	}

	return glyph->width;
}

static uint16_t drawText(
	const Font *font,

	uint16_t *canvas,
	int canvasWidth,

	int x,
	int y,
	int maxWidth,

	const char *string,

	uint16_t fg,
	uint16_t bg
) {
	uint16_t advance = 0;
	uint16_t line = 0;

	while (*string) {
		char character = *string;
		ESP_LOGI(TAG, "draw %c %d %d", character, x, y);

		const Glyph *glyph = findGlyph(font, character);
		uint8_t characterWidth = glyph == NULL ? font->height : glyph->width;

		if (characterWidth + advance > maxWidth) {
			advance = 0;
			line++;
		}

		advance += drawCharacter(
			font, glyph,
			canvas, canvasWidth,
			x + advance, y + line * font->height,
			fg, bg
		);

		string++;
	}

	return line * font->height + font->height;
}

class Display {
	public:
		esp_lcd_panel_io_handle_t port = NULL;
		esp_lcd_panel_handle_t panel = NULL;

		void begin() {
			ESP_LOGI(TAG, "prepare SPI");
			spi_bus_config_t busConfiguration = {};
			busConfiguration.mosi_io_num = PIN_NUM_MOSI;
			busConfiguration.miso_io_num = -1;
			busConfiguration.sclk_io_num = PIN_NUM_CLK;
			busConfiguration.quadwp_io_num = -1;
			busConfiguration.quadhd_io_num = -1;
			busConfiguration.max_transfer_sz = LCD_HEIGHT * 60 * 2; // enough for our small stripe buffer

			ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &busConfiguration, SPI_DMA_CH_AUTO));

			ESP_LOGI(TAG, "prepare port");
			esp_lcd_panel_io_spi_config_t portConfiguration = {};
			portConfiguration.cs_gpio_num = PIN_NUM_CS;
			portConfiguration.dc_gpio_num = PIN_NUM_DC;
			portConfiguration.spi_mode = 0;
			portConfiguration.pclk_hz = 40 * 1000 * 100;
			portConfiguration.trans_queue_depth = 10;
			portConfiguration.lcd_cmd_bits = 8;
			portConfiguration.lcd_param_bits = 8;

			ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_HOST, &portConfiguration, &port));

			ESP_LOGI(TAG, "prepare ST7796 driver");
			esp_lcd_panel_dev_config_t panelConfiguration = {};
			panelConfiguration.reset_gpio_num = PIN_NUM_RST;
			panelConfiguration.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
			panelConfiguration.bits_per_pixel = 16;

			ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(port, &panelConfiguration, &panel));

			ESP_LOGI(TAG, "prepare panel");
			ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
			ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
			ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

			ESP_LOGI(TAG, "orient panel");
			ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
		}

		void presentTag(const char* tag) {
			Frame frame = this->createFrame(
				0, 0,
				LCD_WIDTH, Monospace40.height + 10,
				rgb(0, 0, 0)
			);

			this->renderText(
				&frame,
				tag,
				&Monospace40,

				10,
				0,
				10,

				rgb(255, 255, 255)
			);

			this->renderFrame(&frame);
		}

	private:
		Frame createFrame(
			const uint16_t x,
			const uint16_t y,

			const uint16_t width,
			const uint16_t height,

			uint16_t color
		) {
			Frame frame(x, y, width, height, color);

			frame.canvas = (uint16_t*)heap_caps_malloc(
				width * height * sizeof(uint16_t),
				MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
			);

			ESP_ERROR_CHECK(frame.canvas ? ESP_OK : ESP_ERR_NO_MEM);

			for (int i = 0; i < width * height; i++) {
				frame.canvas[i] = color;
			}

			return frame;
		}

		void renderFrame(Frame *frame) {
			ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
				panel,

				frame->x,
				frame->y,

				frame->width - frame->x,
				frame->height - frame->y,

				frame->canvas
			));

			heap_caps_free(frame->canvas);
		}

		uint16_t renderText(
			const Frame *frame,

			const char* text,
			const Font *font,

			uint16_t left,
			uint16_t top,
			uint16_t right,

			uint16_t color
		) {
			return drawText(
				font,

				frame->canvas,
				frame->width,

				left,
				top,
				frame->width - right,

				text,

				color,
				frame->color
			);
		}
};
