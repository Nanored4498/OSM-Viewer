// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460

layout (location = 0) in vec2 txtCenter;
layout (location = 1) in vec2 offset;
layout (location = 2) in vec2 vUV;
layout (location = 3) in vec2 vUV2;

out vec2 uv;
out vec2 uv2;

uniform vec2 center;
uniform vec2 scale;
uniform vec2 txtScale;

void main() {
	gl_Position = vec4(scale*(txtCenter - center) + txtScale*offset, 0., 1.);
	uv = vUV;
	uv2 = vUV2;
}
