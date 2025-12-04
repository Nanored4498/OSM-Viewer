// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <type_traits>
#include <vector>

template<typename T>
struct HashMap {
protected:
	struct Node {
		std::pair<const int64_t, T> kv;
		int nxt;
		Node(const int64_t id, int nxt): kv{id, T()}, nxt(nxt) {}
	};

	template<bool isConst, typename U>
	using conditional_const = std::conditional_t<isConst, const U, U>;

	template<bool isConst>
	struct _iterator {
	private:
		template<typename U>
		using put_const = std::conditional_t<isConst, const U, U>;

		put_const<Node>* it;
	
	public:
		using iterator_category = std::forward_iterator_tag;
		using difference_type   = std::ptrdiff_t;
		using value_type        = decltype(Node::kv);
		using pointer           = put_const<value_type>*;
		using reference         = put_const<value_type>&;

		_iterator(put_const<Node>* it = nullptr): it(it) {}

		reference operator*() const { return it->kv; }
		pointer operator->() const { return &it->kv; }
		_iterator& operator++() { ++it; return *this; }
		_iterator operator++(int) { _iterator tmp = *this; ++(*this); return tmp; }
		template<bool isConst2>
		bool operator==(const _iterator<isConst2>& other) const { return it == other.it; }
	};

	std::vector<int> buckets;
	std::vector<Node> v;

	inline int key(int64_t id) const {
		return id % buckets.size();
	}

	Node* _find(const int64_t id) {
		if(buckets.empty()) return v.data() + v.size();
		int b = key(id);
		for(int i = buckets[b]; i != -1; i = v[i].nxt)
			if(v[i].kv.first == id) return &v[i];
		return v.data() + v.size();
	}

public:
	using iterator = _iterator<false>;
	using const_iterator = _iterator<true>;

	iterator begin() { return v.data(); }
	iterator end() { return v.data() + v.size(); }
	const_iterator begin() const { return v.data(); }
	const_iterator end() const { return v.data() + v.size(); }

	iterator find(const int64_t id) { return _find(id); }
	const_iterator find(const int64_t id) const { return _find(id); }

	T& operator[](const int64_t id) {
		if(buckets.empty()) buckets.assign(primes[0], -1);
		int b = key(id);
		for(int i = buckets[b]; i != -1; i = v[i].nxt)
			if(v[i].kv.first == id) return v[i].kv.second;
		if(v.size() >= buckets.size()) {
			rehash(nextSize(v.size()+1));
			b = key(id);
		}
		const int i = (int) v.size();
		T& ans = v.emplace_back(id, buckets[b]).kv.second;
		buckets[b] = i;
		return ans;
	}

	bool contains(const int64_t id) const {
		return find(id) != end();
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
			const int b = key(v[i].kv.first);
			v[i].nxt = buckets[b];
			buckets[b] = i;
		}
	}
};
