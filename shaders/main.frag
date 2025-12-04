// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460
#extension GL_ARB_shading_language_include : require

#include "/camera.glsl"

out vec4 fragColor;

uniform vec3 color;

const vec2 tree1center = vec2(0.4, 0.64);
const float tree1rad1 = pow(.12, 2);
const float tree1rad2 = pow(.16, 2);
const vec2 tree1xy1 = tree1center - vec2(0.02, 0.35);
const vec2 tree1xy2 = tree1center + vec2(0.02, -0.14);
const vec2 tree2xy1 = tree1xy1 + vec2(0.3, 0);
const vec2 tree2xy2 = tree2xy1 + vec2(0.04, .15);

void main() {
	fragColor = vec4(color, 1.);
	vec2 uv = fract((gl_FragCoord.xy + center*scale/txtScale) / 40.);
	vec2 uv1 = uv - tree1center;
	float r1 = dot(uv1, uv1);
	if(tree1rad1 < r1 && r1 < tree1rad2) fragColor.xyz *= 0.8;
	else if(all(lessThan(tree1xy1, uv)) && all(lessThan(uv, tree1xy2))) fragColor.xyz *= 0.8;
	else if(all(lessThan(tree2xy1, uv)) && all(lessThan(uv, tree2xy2))) fragColor.xyz *= 0.8;
}
