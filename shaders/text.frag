// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460

in vec2 uv;

out vec4 fragColor;

uniform sampler2D fontAtlas;

void main() {
	float alpha = texture(fontAtlas, uv).r;
	fragColor = vec4(vec3(0.), alpha);
}