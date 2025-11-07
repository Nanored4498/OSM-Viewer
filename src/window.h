// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "glad/gl.h"

#include "font.h"
#include "programs/generated/programs.h"

struct Window {
	void init(float x0, float x1, float y0, float y1);
	void start();

	void updateScale(double add, double x, double y);
	void setAnchor(double x, double y);
	void moveAnchor(double x, double y);
	void setAspect(int width, int height);

// protected:
	GLFWwindow *window;
	int width = 800;
	int height = 600;

	GLuint VAO;
	GLuint textSSBO;
	Programs progs;
	Font::Atlas atlas;

	float centerX, centerY, scale;
	float anchorX, anchorY;

	struct Road {
		float r, g, b;
		float r2, g2, b2;
		GLint first;
		GLsizei count;
		bool border;
	};
	std::vector<Road> roads;
	GLint capitalsFirst;
	GLsizei capitalsCount;
	GLsizei charactersCount;
};
