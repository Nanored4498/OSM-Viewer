// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <cstdint>
#include <concepts>
#include <type_traits> 

namespace Proto {

bool readBool(const uint8_t* &it);
uint32_t readInt32(const uint8_t* &it);
uint64_t readInt64(const uint8_t* &it);

inline auto decodeZigzag(std::unsigned_integral auto z) {
	return static_cast<std::make_signed_t<decltype(z)>>((z>>1) ^ -(z&1));
}
inline int32_t readSint32(const uint8_t* &it) {
	return decodeZigzag(readInt32(it));
}
inline int64_t readSint64(const uint8_t* &it) {
	return decodeZigzag(readInt64(it));
}

}