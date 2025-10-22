// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

template<typename T>
struct HashMap {
protected:
	struct Node {
		int64_t id;
		T v;
		int nxt;
		Node(const int64_t id, int nxt): id(id), nxt(nxt) {}
	};
	std::vector<int> buckets;
	std::vector<Node> v;

public:
	inline int key(int64_t id) const {
		return id % buckets.size();
	}

	T& operator[](const int64_t id) {
		if(buckets.empty()) buckets.assign(primes[0], -1);
		int b = key(id);
		for(int i = buckets[b]; i != -1; i = v[i].nxt)
			if(v[i].id == id) return v[i].v;
		if(v.size() >= buckets.size()) {
			rehash(nextSize(v.size()+1));
			b = key(id);
		}
		const int i = (int) v.size();
		T& ans = v.emplace_back(id, buckets[b]).v;
		buckets[b] = i;
		return ans;
	}

	size_t size() const {
		return v.size();
	}

protected:
	static constexpr size_t primes[] = {7, 17, 37, 79, 163, 331, 673, 1361, 2729, 5471, 10949, 21911,
										43853, 87719, 175447, 350899, 701819, 1403641, 2807303, 5614657,
										11229331, 22458671, 44917381, 89834777, 179669557, 359339171};
	
	static size_t nextSize(size_t n) {
		auto it = std::ranges::lower_bound(primes, n);
		assert(it != primes + std::size(primes));
		return *it;
	}

	void rehash(size_t s) {
		buckets.assign(s, -1);
		const int V = v.size();
		for(int i = 0; i < V; ++i) {
			const int b = key(v[i].id);
			v[i].nxt = buckets[b];
			buckets[b] = i;
		}
	}
};
