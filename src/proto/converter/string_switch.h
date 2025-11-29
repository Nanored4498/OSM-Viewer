// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

struct StringSwitch {
protected:
	inline static constexpr char minChar = ':';
	inline static constexpr char maxChar = 'z';
	struct State {
		union {
			std::array<uint32_t, maxChar-minChar+1> nxt;
			std::pair<const char*, uint32_t> word;
		};
		bool end;
		State() {}
	};
	std::vector<State> states;
	std::vector<uint32_t> starts;

public:
	StringSwitch(const std::vector<std::pair<const char*, uint32_t>> &words);

	inline static constexpr uint32_t NOT_FOUND = -1;
	uint32_t feed(const std::string_view &word) const;
};
