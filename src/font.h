#include <cstdint>
#include <memory>

namespace Font {

constexpr uint8_t firstChar = 32;
constexpr uint8_t endChar = 127;
constexpr uint32_t charCount = endChar - firstChar;

struct CharPosition {
	uint16_t x0, y0, x1, y1;
	float xoff, yoff, xadvance;
};

struct Atlas {
	int width, height;
	std::unique_ptr<uint8_t[]> img;
	CharPosition charPositions[charCount];
};

Atlas getTTFAtlas(const char* fileName, float fontSize);

}