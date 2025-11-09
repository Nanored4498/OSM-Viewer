// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#version 460
#extension GL_ARB_shading_language_include : require

#include "/camera.glsl"

layout (location = 0) in vec2 p;

void main() {
	gl_Position = vec4(scale*(p - center), 0., 1.);
}
