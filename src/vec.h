// Copyright (C) 2023, 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>

template<typename T>
struct get_scalar { using type = typename T::Scalar; };
template<typename T> requires std::is_arithmetic_v<T>
struct get_scalar<T> { using type = T; };
template<typename T>
using get_scalar_t = typename get_scalar<T>::type;

template<int N, typename T, typename V>
struct vec_base {
	using Scalar = get_scalar_t<T>;
	static constexpr int Dim = N;

private:
	// Casting to V
	inline V& toV() { return *reinterpret_cast<V*>(this); }
	inline const V& toV() const { return *reinterpret_cast<const V*>(this); }

public:
	// Constructor
	vec_base() = default;
	template<int M, typename U, typename W> requires(M <= N)
	vec_base(const vec_base<M, U, W> &other);

	// Coordinate access
	inline const T& operator[](std::ptrdiff_t i) const { return reinterpret_cast<const T*>(this)[i]; }
	inline T& operator[](std::ptrdiff_t i) { return reinterpret_cast<T*>(this)[i]; }

	// Unary minus
	inline V operator-() const { V v; for(std::ptrdiff_t i = 0; i < N; ++i) v[i] = -(*this)[i]; return v; }

	// Operators with scalar
	inline V& operator*=(Scalar x) { for(std::ptrdiff_t i = 0; i < N; ++i) (*this)[i] *= x; return toV(); }
	inline V operator*(Scalar x) const { return V(toV()) *= x; }
	inline friend V operator*(Scalar x, const vec_base &v) { return v * x; }
	inline V& operator/=(Scalar x) { for(std::ptrdiff_t i = 0; i < N; ++i) (*this)[i] /= x; return toV(); }
	inline V operator/(Scalar x) const { return V(toV()) /= x; }

	// Operators with vectors
	template<typename W>
	inline V& operator+=(const W& other) { for(std::ptrdiff_t i = 0; i < N; ++i) (*this)[i] += other[i]; return toV(); }
	template<typename U, typename W>
	auto operator+(const vec_base<N, U, W>& other) const;
	template<typename W>
	inline V& operator-=(const W& other) { for(std::ptrdiff_t i = 0; i < N; ++i) (*this)[i] -= other[i]; return toV(); }
	template<typename U, typename W>
	auto operator-(const vec_base<N, U, W>& other) const;

	// Dot product
	template<typename U, typename W>
	auto operator*(const vec_base<N, U, W>& other) const;

	// Equality
	bool operator==(const V& other) const;
	bool operator!=(const V& other) const;

	// Norm
	Scalar norm2() const;
	inline Scalar norm() const { return std::sqrt(norm2()); }
	inline V& normalize() { return *this *= 1. / norm(); }
};

template <typename T>
struct vec2T : vec_base<2, T, vec2T<T>> {
	T x, y;
	vec2T() = default;
	constexpr vec2T(T x, T y): x(x), y(y) {}
	template<int M, typename U, typename W>
	vec2T(const vec_base<M, U, W> &other): vec_base<2, T, vec2T>(other) {}
};
using vec2f = vec2T<float>;
using vec2l = vec2T<int64_t>;

template <typename T>
struct vec3T : vec_base<3, T, vec3T<T>> {
	union {T x; T r;};
	union {T y; T g;};
	union {T z; T b;};
	vec3T() = default;
	constexpr vec3T(T x, T y, T z): x(x), y(y), z(z) {}
	template<int M, typename U, typename W>
	vec3T(const vec_base<M, U, W> &other): vec_base<3, T, vec3T>(other) {}
};
using vec3f = vec3T<float>;

template<typename V>
struct Box {
	V min, max;
	Box() {
		for(std::ptrdiff_t i = 0; i < V::Dim; ++i) {
			min[i] = std::numeric_limits<get_scalar_t<V>>::max();
			max[i] = std::numeric_limits<get_scalar_t<V>>::min();
		}
	}

	template<typename U, typename W>
	void update(const vec_base<V::Dim, U, W> &v) {
		for(std::ptrdiff_t i = 0; i < V::Dim; ++i) {
			if((get_scalar_t<V>) v[i] < min[i]) min[i] = (get_scalar_t<V>) v[i];
			if((get_scalar_t<V>) v[i] > max[i]) max[i] = (get_scalar_t<V>) v[i];
		}
	}

	inline get_scalar_t<V> diag() const { return (max - min).norm(); }
};


////////////////////////
//  IMPLEMENTATIONS   //
////////////////////////


template<int N, typename T, typename V>
template<int M, typename U, typename W> requires(M <= N)
inline vec_base<N, T, V>::vec_base(const vec_base<M, U, W> &other) {
	for(std::ptrdiff_t i = 0; i < M; ++i) (*this)[i] = other[i];
	for(std::ptrdiff_t i = M; i < N; ++i) (*this)[i] = T(0);
}

template<int N, typename T, typename V>
template<typename U, typename W>
inline auto vec_base<N, T, V>::operator+(const vec_base<N, U, W>& other) const {
	using TU = std::common_type_t<T, U>;
	if constexpr (std::is_same_v<TU, T>) return V(toV()) += other;
	else if constexpr (std::is_same_v<TU, U>) return W(other.toV()) += *this;
}

template<int N, typename T, typename V>
template<typename U, typename W>
inline auto vec_base<N, T, V>::operator-(const vec_base<N, U, W>& other) const {
	using TU = std::common_type_t<T, U>;
	if constexpr (std::is_same_v<TU, T>) return V(toV()) -= other;
	else if constexpr (std::is_same_v<TU, U>) return W(*this) -= other;
}

template<int N, typename T, typename V>
template<typename U, typename W>
inline auto vec_base<N, T, V>::operator*(const vec_base<N, U, W>& other) const {
	decltype((*this)[0]*other[0]) d = 0.;
	for(std::ptrdiff_t i = 0; i < N; ++i)
		d += (*this)[i]*other[i];
	return d;
}

template<int N, typename T, typename V>
inline bool vec_base<N, T, V>::operator==(const V& other) const {
	for(std::ptrdiff_t i = 0; i < N; ++i)
		if((*this)[i] != other[i])
			return false;
	return true;
}
template<int N, typename T, typename V>
inline bool vec_base<N, T, V>::operator!=(const V& other) const {
	for(std::ptrdiff_t i = 0; i < N; ++i)
		if((*this)[i] != other[i])
			return true;
	return false;
}

template<int N, typename T, typename V>
inline vec_base<N, T, V>::Scalar vec_base<N, T, V>::norm2() const {
	Scalar n2 = 0.;
	for(std::ptrdiff_t i = 0; i < N; ++i)
		n2 += (*this)[i]*(*this)[i];
	return n2;
}

template<int N, typename T, typename V>
inline std::ostream& operator<<(std::ostream &stream, const vec_base<N, T, V> &v) {
	stream << '(' << v[0];
	for(std::ptrdiff_t i = 1; i < N; ++i) stream << ", " << v[i];
	return stream << ')';
}