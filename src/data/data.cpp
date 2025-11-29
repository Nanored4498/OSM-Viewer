// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include "data.h"

#include <fstream>

using namespace std;

template<typename T>
static void writeValue(ostream &out, const T &x) {
	out.write(reinterpret_cast<const char*>(&x), sizeof(T));
}

template<typename T>
static void writeVector(ostream &out, const vector<T> &v) {
	volatile uint32_t size = v.size();
	out.write(reinterpret_cast<const char*>(const_cast<uint32_t*>(&size)), sizeof(size));
	out.write(reinterpret_cast<const char*>(v.data()), v.size()*sizeof(T));
}

void OSMData::write(const char *fileName) const {
	ofstream out(fileName);
	writeValue(out, bbox);
	writeVector(out, roads);
	writeVector(out, roadOffsets);
	writeValue(out, roadTypeOffsets);
	writeValue(out, waterWayTypeOffsets);
	writeValue(out, boundaries);
	writeValue(out, forests);
}
