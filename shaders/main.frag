// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460

out vec4 fragColor;

uniform vec3 color;

void main() {
	fragColor = vec4(color, 1.);
}