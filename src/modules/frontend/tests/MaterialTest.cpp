#include "core/tests/AbstractTest.h"
#include "image/Image.h"
#include "video/Color.h"
#include <glm/gtc/noise.hpp>
#include <glm/gtc/constants.hpp>

namespace noise {

class MaterialTest: public core::AbstractTest {
protected:
	const int components = 4;
	const int w = 256;
	const int h = 256;

	bool WriteImage(const char* name, uint8_t* buffer, int w = 256, int h = 256, int components = 4) {
		return image::Image::writePng(name, buffer, w, h, components);
	}
};

TEST_F(MaterialTest, testMaterial) {
	const int w = 256;
	const int h = 512;
	uint8_t buffer[w * h * components];

	#include "frontend/MaterialColor.h"

	auto line_height = h / 32;

	int y = 0;
	for (int color_index = 0; color_index < 32; ++color_index) {
		auto line_y = 0;
		auto color = materialColors[color_index];
		for (; line_y < line_height; ++y, ++line_y) {
			for (int x = 0; x < w; ++x) {
				int index = y * (w * components) + (x * components);
				buffer[index++] = line_y == 0 ? 0 : color.r * 255;
				buffer[index++] = line_y == 0 ? 0 : color.g * 255;
				buffer[index++] = line_y == 0 ? 0 : color.b * 255;
				buffer[index++] = line_y == 0 ? 255 : color.a * 255;
			}
		}
	}
	ASSERT_TRUE(WriteImage("material.png", buffer, w, h));
}

}
