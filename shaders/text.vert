// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460
#extension GL_ARB_shading_language_include : require

#include "/camera.glsl"

struct GlyphData {
	vec2 txtCenter;
	vec2 offset;
	vec2 size;
	vec2 uv;
	vec2 uvSize;
};

layout (binding = 0, std430) readonly buffer ssbo {
	GlyphData glyphs[];
};

uniform vec2 txtScale;

out vec2 uv;

const vec2 off[6] = vec2[6](
	vec2(0., 0.),
	vec2(0., 1.),
	vec2(1., 1.),
	vec2(0., 0.),
	vec2(1., 1.),
	vec2(1., 0.)
);

void main() {
	int i = gl_VertexID / 6;
	int j = gl_VertexID % 6;
	vec2 o = off[j];

	vec2 txtCenter = glyphs[i].txtCenter;
	vec2 offset = glyphs[i].offset + o * glyphs[i].size;
	uv = glyphs[i].uv + o * glyphs[i].uvSize;

	gl_Position = vec4(scale*(txtCenter - center) + txtScale*offset, 0., 1.);
}
