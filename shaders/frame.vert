// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460
#extension GL_ARB_shading_language_include : require

#include "/camera.glsl"

layout (location = 0) in vec2 txtCenter;
layout (location = 1) in vec2 offset;
layout (location = 2) in vec2 size;

out vec2 uv;
flat out vec2 vSize;

const vec2 off[4] = vec2[4](
	vec2(0., 0.),
	vec2(1., 0.),
	vec2(0., 1.),
	vec2(1., 1.)
);

const float border = 2.67;

void main() {
	uv = off[gl_VertexID];

	vec2 offset = offset + uv * size;

	uv = 2. * uv - vec2(1.);
	vSize = vec2(2.*border) / size;

	gl_Position = vec4(scale*(txtCenter - center) + txtScale*offset, 0., 1.);
}
