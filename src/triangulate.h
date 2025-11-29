// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#pragma once

#include <cstdint>
#include <vector>

#include <vec.h>

std::vector<uint32_t> triangulate(const vec2l *pts, int N);