// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include "data.h"

#include <fstream>

using namespace std;

template<typename T>
static void readValue(istream &in, T &x) {
	in.read(reinterpret_cast<char*>(&x), sizeof(T));
}

template<typename T>
static void readVector(istream &in, vector<T> &v) {
	volatile uint32_t size;
	in.read(reinterpret_cast<char*>(const_cast<uint32_t*>(&size)), sizeof(size));
	v.resize(size);
	in.read(reinterpret_cast<char*>(v.data()), v.size()*sizeof(T));
}

void OSMData::read(const char *fileName) {
	ifstream in(fileName);
	readValue(in, bbox);
	readVector(in, roads);
	readVector(in, roadOffsets);
	readValue(in, roadTypeOffsets);
	readValue(in, waterWayTypeOffsets);
	readValue(in, boundaries);
	readValue(in, forests);
	readVector(in, names);
	readVector(in, capitals);
	readVector(in, roadNames);
}

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
	writeVector(out, names);
	writeVector(out, capitals);
	writeVector(out, roadNames);
}
