// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#pragma once

#include <cstdint>
#include <vector>

#include <vec.h>

std::vector<uint32_t> triangulate(const vec2l *pts, const uint32_t *ends, uint32_t Nloops, uint32_t Nout);
inline std::vector<uint32_t> triangulate(const vec2l *pts, uint32_t N) {
	volatile const uint32_t end = N;
	return triangulate(pts, const_cast<const uint32_t*>(&end), 1u, 1u);
}
