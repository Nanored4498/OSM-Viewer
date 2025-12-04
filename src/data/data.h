// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#pragma once

#include <array>
#include <vector>

#include "vec.h"

// TODO: when granularity is 100 could use int32_t as coordinates

enum class RoadType : uint32_t {
	MOTORWAY,
	TRUNK,
	PRIMARY,
	NUM
};

enum class WaterWayType : uint32_t {
	RIVER,
	NUM
};

struct OSMData {
	// boudning box
	Box<vec2l> bbox;

	// polylines
	std::vector<vec2l> roads;
	std::vector<uint32_t> roadOffsets;
	std::array<uint32_t, (size_t) RoadType::NUM + 1> roadTypeOffsets;
	std::array<uint32_t, (size_t) WaterWayType::NUM + 1> waterWayTypeOffsets;
	std::pair<uint32_t, uint32_t> boundaries;

	// areas
	std::vector<uint32_t> refs;
	std::vector<uint32_t> refOffsets;
	std::pair<uint32_t, uint32_t> forests, forestsR;

	// named points
	std::vector<char> names;
	std::vector<std::pair<vec2l, uint32_t>> capitals;
	// TODO: should be a list of roads and not a point
	std::vector<std::pair<vec2l, uint32_t>> roadNames;

	// Helper
	bool isWayClosed(uint32_t id) const;

	// IO
	void read(const char *fileName); 
	void write(const char *fileName) const;
};