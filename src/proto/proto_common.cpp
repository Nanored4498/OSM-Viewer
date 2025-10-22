// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include "proto_common.h"

#include <cstdlib>
#include <iostream>

using namespace std;

namespace Proto {

bool readBool(const uint8_t* &it) {
	if(*it > 1) {
		cerr << "Value too big for a bool....\n";
		exit(1);
	}
	return *(it++);
}

uint32_t readInt32(const uint8_t* &it) {
	uint32_t value = *it & 0x7f;
	if(!(*(it++) & 0x80u)) return value;
	value |= uint32_t(*it & 0x7f) << 7;
	if(!(*(it++) & 0x80u)) return value;
	value |= uint32_t(*it & 0x7f) << 14;
	if(!(*(it++) & 0x80u)) return value;
	value |= uint32_t(*it & 0x7f) << 21;
	if(!(*(it++) & 0x80u)) return value;
	if(*it > 0b1111u) {
		cerr << "Value too big for a 32 bits varint....\n";
		exit(1);
	}
	value |= uint32_t(*it) << 28;
	return value;
}
	
uint64_t readInt64(const uint8_t* &it) {
	uint64_t value = *it & 0x7f;
	if(!(*(it++) & 0x80u)) return value;
	value |= uint64_t(*it & 0x7f) << 7;
	if(!(*(it++) & 0x80u)) return value;
	value |= uint64_t(*it & 0x7f) << 14;
	if(!(*(it++) & 0x80u)) return value;
	value |= uint64_t(*it & 0x7f) << 21;
	if(!(*(it++) & 0x80u)) return value;
	value |= uint64_t(*it & 0x7f) << 28;
	if(!(*(it++) & 0x80u)) return value;
	value |= uint64_t(*it & 0x7f) << 35;
	if(!(*(it++) & 0x80u)) return value;
	value |= uint64_t(*it & 0x7f) << 42;
	if(!(*(it++) & 0x80u)) return value;
	value |= uint64_t(*it & 0x7f) << 49;
	if(!(*(it++) & 0x80u)) return value;
	value |= uint64_t(*it & 0x7f) << 56;
	if(!(*(it++) & 0x80u)) return value;
	if(*it > 0b1u) {
		cerr << "Value too big for a 64 bits varint....\n";
		exit(1);
	}
	value |= uint64_t(*it) << 63;
	return value;
}
	
}