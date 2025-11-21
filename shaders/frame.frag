// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460

in vec2 uv;
flat in vec2 size;

out vec4 fragColor;

const float border = 4.;
const float d1 = 1. - 1./(2.*border);
const float d2 = 1. + 1./(2.*border);

void main() {
	vec2 duv = vec2(1.)-abs(uv);
	bvec2 comp = lessThan(duv, size);
	if(all(comp)) {
		duv /= size;
		duv = vec2(1.)-duv;
		float d = dot(duv, duv);
		if(d > d2*d2) discard;
		fragColor = vec4(vec3(1.), d < d1*d1 ? 1. : border*(d2-sqrt(d)));
	} else if(any(comp)) {
		fragColor = vec4(1.);
	} else {
		fragColor = vec4(0.835, 0., 0., 1.);
	}
}