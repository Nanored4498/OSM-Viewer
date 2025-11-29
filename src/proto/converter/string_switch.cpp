// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include "string_switch.h"

#include <algorithm>
#include <cstring>
#include <memory>

using namespace std;

StringSwitch::StringSwitch(const std::vector<std::pair<const char*, uint32_t>> &words) {
	if(words.empty()) return;
	using PUU = pair<uint32_t, uint32_t>;
	unique_ptr<PUU[]> order(new PUU[words.size()]);
	size_t nwords = 0;
	for(uint32_t i = 0; i < words.size(); ++i)
		if(words[i].first)
			order[nwords++] = {i, strlen(words[i].first)};
	if(!nwords) return;
	ranges::sort(order.get(), order.get()+nwords, [&](const PUU &a, const PUU &b) {
		return a.second < b.second || (a.second == b.second && strcmp(words[a.first].first, words[b.first].first));
	});
	starts.resize(order[nwords-1].second+1, NOT_FOUND);
	for(uint32_t i = 0; i < nwords;) {
		const uint32_t size = order[i].second;
		uint32_t j = i+1;
		while(j < nwords && order[j].second == size) ++ j;
		starts[size] = states.size();
		auto &info = states.emplace_back().nxt;
		info[0] = 0;
		info[1] = i;
		info[2] = j;
		i = j;
	}
	for(uint32_t st = 0; st < states.size(); ++st) {
		const uint32_t i = states[st].nxt[0];
		uint32_t k = states[st].nxt[1];
		const uint32_t l = states[st].nxt[2];
		if(l-k == 1) {
			states[st].end = true;
			states[st].word = words[order[k].first];
			states[st].word.first += i;
		} else {
			states[st].end = false;
			states[st].nxt.fill(NOT_FOUND);
			while(k < l) {
				const char c = words[order[k].first].first[i];
				states[st].nxt[c-minChar] = states.size();
				auto &info = states.emplace_back().nxt;
				info[0] = i+1;
				info[1] = k;
				while(++k < l && words[order[k].first].first[i] == c);
				info[2] = k;
			}
		}
	}
}

uint32_t StringSwitch::feed(const std::string_view &word) const {
	const size_t size = word.size();
	if(size >= starts.size()) return NOT_FOUND;
	uint32_t st = starts[size], i = 0;
	while(true) {
		if(st == NOT_FOUND) return NOT_FOUND;
		const State &state = states[st];
		if(state.end) return memcmp(word.begin()+i, state.word.first, size-i) ? NOT_FOUND : state.word.second;
		const char c = word[i++];
		if(c < minChar || c > maxChar) return NOT_FOUND;
		st = state.nxt[c-minChar];
	}
}