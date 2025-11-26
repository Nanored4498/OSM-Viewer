// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include "window.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <ranges>

#include "utils.h"

using namespace std;

static void scrollCallback(GLFWwindow* window, [[maybe_unused]] double xoffset, double yoffset) {
	double x, y;
	glfwGetCursorPos(window, &x, &y);
	((Window*) glfwGetWindowUserPointer(window))->updateScale(yoffset, x, y);
}

static void mouseButtonCallback(GLFWwindow* window, int button, int action, [[maybe_unused]] int mods) {
	if(action != GLFW_PRESS) return;
	if(button == GLFW_MOUSE_BUTTON_LEFT) {
		double x, y;
		glfwGetCursorPos(window, &x, &y);
		((Window*) glfwGetWindowUserPointer(window))->setAnchor(x, y);
	}
}

static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
	if(glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) return;
	((Window*) glfwGetWindowUserPointer(window))->moveAnchor(xpos, ypos);
}

static void frameBufferSizeCallback(GLFWwindow *window, int width, int height) {
	glViewport(0, 0, width, height);
	((Window*) glfwGetWindowUserPointer(window))->setAspect(width, height);
}

void Window::updateScale(double add, double x, double y) {
	const float oldScale = scale;
	scale *= exp(0.125*add);
	centerX += (2*x - width) * (1/oldScale - 1/scale);
	centerY += (height - 2*y) * (1/oldScale - 1/scale);
}

void Window::setAnchor(double x, double y) {
	anchorX = (2. * x - width) / scale + centerX;
	anchorY = (height - 2. * y) / scale + centerY;
}

void Window::moveAnchor(double x, double y) {
	centerX = anchorX - (2. * x - width) / scale;
	centerY = anchorY - (height - 2. * y) / scale;
}

void Window::setAspect(int width, int height) {
	this->width = width;
	this->height = height;
}

void Window::init(const vec2f &v0, const vec2f &v1) {
	if(!glfwInit()) THROW_ERROR("Failed to init glfw!");
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	#ifndef NDEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
	#endif
	glfwWindowHint(GLFW_SAMPLES, 4);

	// create window
	window = glfwCreateWindow(width, height, "OSM", nullptr, nullptr);
	glfwSetWindowUserPointer(window, this);
	glfwMakeContextCurrent(window);
	gladLoadGL(glfwGetProcAddress);
	glfwSwapInterval(1);

	// callbacks
	glfwSetScrollCallback(window, scrollCallback);
	glfwSetMouseButtonCallback(window, mouseButtonCallback);
	glfwSetCursorPosCallback(window, cursorPosCallback);
	glfwSetFramebufferSizeCallback(window, frameBufferSizeCallback);
	glfwGetFramebufferSize(window, &width, &height);
	frameBufferSizeCallback(window, width, height);

	// Use alpha
	glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);

	// create program
	progs.init();

	// setup camera
	centerX = (v0.x + v1.x) / 2.;
	centerY = (v0.y + v1.y) / 2.;
	scale = 2.f * min(width/(v1.x - v0.x), height/(v1.y - v0.y));

	// Load fonts
	atlas = Font::getTTFAtlas({
		{
			capitalFont,
			FONT_DIR "/Roboto-Medium.ttf",
			24.f
		}, {
			roadFont,
			FONT_DIR "/Roboto-Bold.ttf",
			16.f
		}
	});
	GLuint fontAtlasTexture;
	glGenTextures(1, &fontAtlasTexture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, fontAtlasTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlas.width, atlas.height, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.img.get());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	progs.text.set_fontAtlas(0);
	atlas.img.~unique_ptr();

	// UBO
	glCreateBuffers(1, &UBO);
	glNamedBufferStorage(UBO, 6 * sizeof(float), nullptr, GL_DYNAMIC_STORAGE_BIT);
	progs.main.bind_Camera(UBO);
	progs.capital.bind_Camera(UBO);
	progs.text.bind_Camera(UBO);

	// TODO: to try
	// glEnable(GL_LINE_SMOOTH);
	// glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	// GL_LINE_STRIP instead of GL_LINES
}

void Window::start() {
	while(!glfwWindowShouldClose(window)) {
		// Clear
		glClearColor(0.945f, 0.933f, 0.910f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);

		// Set UBO
		const float UBOdata[6] {
			centerX, centerY, // center
			scale/width, scale/height, // scale
			2.f/width, 2.f/height // txtScale
		};
		glNamedBufferSubData(UBO, 0, sizeof(UBOdata), UBOdata);

		glBindVertexArray(VAO);
		progs.main.use();

		// Render forests
		if(scale > 26e3f) {
			// TODO: draw trees icon either with frag shader or with texture
			progs.main.set_color(0.675f, 0.824f, 0.612f);
			glDrawElements(GL_TRIANGLES, forestsCount, GL_UNSIGNED_INT, 0);
		}

		// Render roads
		// TODO: rivers should be rendered before road borders
		// TODO: Use one glMultiDrawArraysIndirect and remove color uniform for a SSBO of color per draw using gl_DrawID
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cmdBuffer);
		glLineWidth(5.f);
		for(const Road &r : roads | views::reverse) {
			if(!r.border) continue;
			progs.main.set_color(r.r2, r.g2, r.b2);
			glMultiDrawArraysIndirect(GL_LINE_STRIP, r.offset, r.count, 0);
		}
		glLineWidth(3.f);
		for(const Road &r : roads | views::reverse) {
			progs.main.set_color(r.r, r.g, r.b);
			glMultiDrawArraysIndirect(GL_LINE_STRIP, r.offset, r.count, 0);
		}

		// Render capitals
		progs.capital.use();
		glPointSize(12.f);
		glDrawArrays(GL_POINTS, capitalsFirst, capitalsCount);

		// Render frames
		glBindVertexArray(frameVAO);
		progs.frame.use();
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, framesCount);
	
		// Render text
		glBindVertexArray(textVAO);
		progs.text.use();
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, charactersCount);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}
	glfwDestroyWindow(window);
	glfwTerminate();
}
