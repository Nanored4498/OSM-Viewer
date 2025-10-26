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

static GLint succes;
static GLchar infoLog[1024];

static GLint compileShaderFile(GLuint shader, const char* fileName) {
	ifstream file(fileName, ios::ate);
	if(!file) THROW_ERROR("Failed to open shader file:" + string(fileName));
	const GLint size = (GLint) file.tellg();
	char* src = new char[size];
	file.seekg(0);
	file.read(src, size);
	file.close();
	glShaderSource(shader, 1, &src, &size);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &succes);
	if(!succes) glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
	return succes;
}

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

void Window::init(float x0, float x1, float y0, float y1) {
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

	// create program
	const GLuint vert_shader = glCreateShader(GL_VERTEX_SHADER);
	const GLuint frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
	if(!compileShaderFile(vert_shader, SHADER_DIR "/main.vert"))
		THROW_ERROR(string("Failed to compile vertex shader: " SHADER_DIR "/main.vert\n") + infoLog);
	if(!compileShaderFile(frag_shader, SHADER_DIR "/main.frag"))
		THROW_ERROR(string("Failed to compile fragment shader: " SHADER_DIR "/main.frag\n") + infoLog);
	prog = glCreateProgram();
	glAttachShader(prog, vert_shader);
	glAttachShader(prog, frag_shader);
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &succes);
	if(!succes) {
		glGetProgramInfoLog(prog, sizeof(infoLog), nullptr, infoLog);
		THROW_ERROR(string("Failed to link program: \n") + infoLog);
	}
	glDeleteShader(vert_shader);
	glDeleteShader(frag_shader);

	// get locations
	pLoc = glGetAttribLocation(prog, "p");
	colorLoc = glGetUniformLocation(prog, "color");
	centerLoc = glGetUniformLocation(prog, "center");
	scaleLoc = glGetUniformLocation(prog, "scale");

	// setup camera
	centerX = (x0 + x1) / 2.;
	centerY = (y0 + y1) / 2.;
	scale = 2.f * min(width/(x1 - x0), height/(y1 - y0));

	// TODO: to try
	// glEnable(GL_LINE_SMOOTH);
	// glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	// GL_LINE_STRIP instead of GL_LINES
}

void Window::start() {
	while(!glfwWindowShouldClose(window)) {
		glClearColor(0.945f, 0.933f, 0.910f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);
		glUseProgram(prog);
		glUniform2f(centerLoc, centerX, centerY);
		glUniform2f(scaleLoc, scale/width, scale/height);

		glBindVertexArray(VAO);
		glLineWidth(5.f);
		for(const Road &r : roads | views::reverse) {
			if(!r.border) continue;
			glUniform3f(colorLoc, r.r2, r.g2, r.b2);
			glDrawArrays(GL_LINES, r.first, r.count);
		}
		glLineWidth(3.f);
		for(const Road &r : roads | views::reverse) {
			glUniform3f(colorLoc, r.r, r.g, r.b);
			glDrawArrays(GL_LINES, r.first, r.count);
		}

		glfwSwapBuffers(window);
		glfwPollEvents();
	}
	glfwDestroyWindow(window);
	glfwTerminate();
}
