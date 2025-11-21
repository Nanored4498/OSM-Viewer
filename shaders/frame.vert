// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460
#extension GL_ARB_shading_language_include : require

#include "/camera.glsl"

struct FrameData {
	vec2 txtCenter;
	vec2 offset;
	vec2 size;
};

layout (binding = 0, std430) readonly buffer ssbo {
	FrameData frames[];
};

out vec2 uv;
flat out vec2 size;

const vec2 off[6] = vec2[6](
	vec2(0., 0.),
	vec2(0., 1.),
	vec2(1., 1.),
	vec2(0., 0.),
	vec2(1., 1.),
	vec2(1., 0.)
);

const float border = 4.;

void main() {
	int i = gl_VertexID / 6;
	int j = gl_VertexID % 6;

	uv = off[j];

	vec2 txtCenter = frames[i].txtCenter;
	vec2 offset = frames[i].offset + uv * frames[i].size;

	uv = 2. * uv - vec2(1.);
	size = vec2(2.*border) / frames[i].size;

	gl_Position = vec4(scale*(txtCenter - center) + txtScale*offset, 0., 1.);
}
