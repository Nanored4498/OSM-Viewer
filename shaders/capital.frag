// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460

out vec4 fragColor;

const float pointSize = 12.;
const float d1 = 1./6.-1./(2.*pointSize);
const float d2 = 1./6.+1./(2.*pointSize);
const float d3 = 1./3.-1./(2.*pointSize);
const float d4 = 1./3.+1./(2.*pointSize);

void main() {
	vec2 c = gl_PointCoord - vec2(0.5);
	float d = dot(c, c);
	if(d > 0.25) discard;
	d = sqrt(d);
	float g = d < d1 || d > d4 ? 0. :
			d < d2 ? pointSize*(d - d1) : 
			d < d3 ? 1. :
			pointSize*(d4 - d);
	fragColor = vec4(vec3(g), 1.);
}