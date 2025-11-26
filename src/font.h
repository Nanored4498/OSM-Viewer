// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Font {

constexpr uint8_t firstChar = 32;
constexpr uint8_t endChar = 127;
constexpr uint32_t charCount = endChar - firstChar;

struct CharPosition {
	uint16_t x0, y0, x1, y1;
	float xoff, yoff, xadvance;
};
using CharPositions = std::array<CharPosition, charCount>;

struct Entry {
	CharPositions &positions;
	const char* fileName;
	float fontSize;
};

struct Atlas {
	int width, height;
	std::unique_ptr<uint8_t[]> img;
};

Atlas getTTFAtlas(const std::vector<Entry> &entries);

}