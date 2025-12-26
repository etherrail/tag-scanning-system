#include <stdio.h>

struct Glyph {
	char character;
	uint8_t width;
	const uint16_t *segments;
	uint8_t segmentCount;
};

struct Font {
	uint8_t height;
	const Glyph *glyphs;
	uint8_t glyphCount;
};
