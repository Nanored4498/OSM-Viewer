// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include "data.h"

#include <fstream>

using namespace std;

bool OSMData::isWayClosed(const uint32_t id) const {
	if(forests.first <= id && id < forests.second) return true;
	return false;
}

#define VALUES\
	bbox,\
	roads,\
	roadOffsets,\
	roadTypeOffsets,\
	waterWayTypeOffsets,\
	boundaries,\
	refs,\
	refOffsets,\
	forests,\
	forestsR,\
	names,\
	capitals,\
	roadNames\

static void readData(istream &) {}

template<typename T, typename... Ts>
static void readData(istream &in, T &x, Ts &... xs) {
	in.read(reinterpret_cast<char*>(&x), sizeof(T));
	readData(in, xs...);
}

template<typename T, typename... Ts>
static void readData(istream &in, vector<T> &v, Ts &... xs) {
	volatile uint32_t size;
	in.read(reinterpret_cast<char*>(const_cast<uint32_t*>(&size)), sizeof(size));
	v.resize(size);
	in.read(reinterpret_cast<char*>(v.data()), v.size()*sizeof(T));
	readData(in, xs...);
}

void OSMData::read(const char *fileName) {
	ifstream in(fileName);
	readData(in, VALUES);
}

static void writeData(ostream &) {}

template<typename T, typename... Ts>
static void writeData(ostream &out, const T &x, Ts const&... xs) {
	out.write(reinterpret_cast<const char*>(&x), sizeof(T));
	writeData(out, xs...);
}

template<typename T, typename... Ts>
static void writeData(ostream &out, const vector<T> &v, Ts const&... xs) {
	volatile uint32_t size = v.size();
	out.write(reinterpret_cast<const char*>(const_cast<uint32_t*>(&size)), sizeof(size));
	out.write(reinterpret_cast<const char*>(v.data()), v.size()*sizeof(T));
	writeData(out, xs...);
}

void OSMData::write(const char *fileName) const {
	ofstream out(fileName);
	writeData(out, VALUES);
}
