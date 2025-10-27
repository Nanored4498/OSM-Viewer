// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460

out vec4 fragColor;

void main() {
	vec2 c = gl_PointCoord - vec2(0.5);
	float d = dot(c, c);
	if(d > 0.25) discard;
	fragColor = vec4(vec3(d > 0.11 || d < 0.028 ? 0. : 1.), 1.);
}