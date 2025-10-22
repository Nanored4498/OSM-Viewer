// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460

layout (location = 0) in vec2 p;

uniform vec2 center;
uniform vec2 scale;

void main() {
	gl_Position = vec4(scale*(p - center), 0., 1.);
}
