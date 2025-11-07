// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460

in vec2 uv;
in vec2 uv2;

out vec4 fragColor;

uniform sampler2D fontAtlas;

void main() {
	float b = texture(fontAtlas, uv).r;
	float w = texture(fontAtlas, uv2).r;
	float alpha = b+w-b*w;
	if(alpha < 1e-3) discard;
	float gray = w * (1. - b) / alpha;
	fragColor = vec4(vec3(gray), alpha);
}