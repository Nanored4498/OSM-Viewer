// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460

in vec2 uv;

out vec4 fragColor;

uniform sampler2D fontAtlas;

void main() {
	fragColor = vec4(vec3(1. - texture(fontAtlas, uv).r), 1.);
}