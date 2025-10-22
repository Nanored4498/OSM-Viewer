// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numbers>
#include <unordered_set>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "glad/gl.h"

// TODO: rewrite decompression
#include <zlib.h>

#include "hashmap.h"
#include "utils.h"

#include "proto/generated/osm.pb.h"

using namespace std;

struct BinStream : public ifstream {
	BinStream(const char* filename): ifstream(filename) {}
	template<integral T>
	BinStream& readInt(T &value) {
		read(reinterpret_cast<char*>(&value), sizeof(T));
		if constexpr (endian::native == endian::little)
			value = byteswap(value);
		return *this;
	}
};

unordered_set<string> supported_features = {
	"OsmSchema-V0.6",
	"DenseNodes",
};

struct Node {
	int64_t lon, lat;
};

HashMap<Node> nodes;
vector<vector<int64_t>> motorways, departments;

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

int main() {
	BinStream input("map/lorraine-latest.osm.pbf");
	uint32_t blobHeaderSize;
	vector<uint8_t> wire;
	bool hasHeader = false;
	while(input.readInt(blobHeaderSize)) {
		wire.resize(blobHeaderSize);
		input.read(reinterpret_cast<char*>(wire.data()), wire.size());
		const Proto::BlobHeader header(wire);
		wire.resize(header.datasize);
		input.read(reinterpret_cast<char*>(wire.data()), wire.size());
		Proto::Blob blob(wire);
		vector<uint8_t> data(blob.raw_size);
		if(blob._data_choice == Proto::Blob::DATA_ZLIB_DATA) {
			uLongf data_size = blob.raw_size;
			if(uncompress(data.data(), &data_size, blob.data.zlib_data.data(), blob.data.zlib_data.size()) != Z_OK) {
				cerr << "Failed to uncompress...\n";
				return 1;
			}
		} else {
			cerr << "Uncompression of blob data not implemented " << blob._data_choice << endl;
			exit(1);
		}
		if(header.type == "OSMHeader") {
			if(hasHeader) {
				cerr << "multiple OSMHeader..." << endl;
				exit(1);
			}
			hasHeader = true;
			const Proto::HeaderBlock hb(data);
			if(hb._has_bbox) {
				cout << "BBOX:\n\t";
				cout << hb.bbox.bottom << ' ' << hb.bbox.top << ' ' << hb.bbox.left << ' ' << hb.bbox.right << endl;
			}
			for(const string &feature : hb.required_features) {
				if(!supported_features.count(feature)) {
					cerr << "Not supported required feature: " << feature << endl;
					exit(1);
				}
			}
			cout << "Optional features:\n";
			for(const string &s : hb.optional_features)
				cout << '\t' << s << endl;
		} else if(header.type == "OSMData") {
			if(!hasHeader) {
				cerr << "OSMData blob before any OSMHeader..." << endl;
				exit(1);
			}
			Proto::PrimitiveBlock pb(data);
			const auto &ST = pb.stringtable.s;
			for(Proto::PrimitiveGroup &pg : pb.primitivegroup) {
				for([[maybe_unused]] const Proto::Node &node : pg.nodes) {
					cerr << "Not implemented (" << __FILE__ << ':' << __LINE__ << ")\n";
					exit(1);
				}
				for(Proto::Way &way : pg.ways) {
					if(!way.lat.empty() || !way.lon.empty()) {
						cerr << "lat and lon fields in Way are not supported (" << __FILE__ << ':' << __LINE__ << ")\n";
						exit(1);
					}
					const int T = way.keys.size();
					if(T != (int) way.vals.size()) {
						cerr << "Sizes mismatch in way's tags... (" << __FILE__ << ':' << __LINE__ << ")\n";
						exit(1);
					}
					const int R = way.refs.size();
					for(int i = 1; i < R; ++i)
						way.refs[i] += way.refs[i-1];
					bool admin_boundary = false;
					bool department_lvl = false;
					for(int i = 0; i < T; ++i) {
						const string key(ST[way.keys[i]].begin(), ST[way.keys[i]].end());
						const string val(ST[way.vals[i]].begin(), ST[way.vals[i]].end());
						if(key == "highway" && val == "motorway")
							motorways.push_back(way.refs);
						else if(key == "boundary" && val == "administrative")
							admin_boundary = true;
						else if(key == "admin_level" && val[0] <= '7')
							department_lvl = true;
					}
					if(admin_boundary && department_lvl)
						departments.push_back(way.refs);
					// WARNING: We don't care about Info here
				}
				for([[maybe_unused]] const Proto::Relation &relation : pg.relations) {
					// WARNING: Currently not implemented
				}
				for([[maybe_unused]] const Proto::ChangeSet &changeset : pg.changesets) {
					cerr << "Not implemented (" << __FILE__ << ':' << __LINE__ << ")\n";
					exit(1);
				}
				if(pg._has_dense) {
					const Proto::DenseNodes &dense = pg.dense;
					const int N = dense.id.size();
					if(N != (int) dense.lat.size() || N != (int) dense.lon.size()) {
						cerr << "Sizes mismatch in denseNodes... (" << __FILE__ << ':' << __LINE__ << ")\n";
						exit(1);
					}
					auto kv_it = dense.keys_vals.begin();
					int64_t id = 0, lat = 0, lon = 0;
					for(int i = 0; i < N; ++i) {
						id += dense.id[i];
						lat += dense.lat[i];
						lon += dense.lon[i];
						Node &node = nodes[id];
						node.lon = pb.lon_offset + pb.granularity * lon;
						node.lat = pb.lat_offset + pb.granularity * lat;
						while(*kv_it) {
							const string key(ST[*kv_it].begin(), ST[*kv_it].end());
							++ kv_it;
							const string val(ST[*kv_it].begin(), ST[*kv_it].end());
							++ kv_it;
						}
						++ kv_it;
					}
					if(kv_it != dense.keys_vals.end()) {
						cerr << "Sizes mismatch in denseNodes... (" << __FILE__ << ':' << __LINE__ << ")\n";
						exit(1);
					}
					// WARNING: We don't care about DenseInfo here
				}
			}
		} else {
			cerr << "Not recognized blob type: " << header.type << endl;
			exit(1);
		}
	}

	int windowWidth = 800;
	int windowHeight = 600;
	if(!glfwInit()) {
		cerr << "Failed to init GLFW\n";
		exit(1);
	}
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	#ifndef NDEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
	#endif
	glfwWindowHint(GLFW_SAMPLES, 4);
	GLFWwindow *window = glfwCreateWindow(windowWidth, windowHeight, "OSM", nullptr, nullptr);
	glfwMakeContextCurrent(window);
	gladLoadGL(glfwGetProcAddress);
	glfwSwapInterval(1);

	// create program
	const GLuint vert_shader = glCreateShader(GL_VERTEX_SHADER);
	const GLuint frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
	if(!compileShaderFile(vert_shader, SHADER_DIR "/main.vert"))
		THROW_ERROR(string("Failed to compile vertex shader: " SHADER_DIR "/main.vert\n") + infoLog);
	if(!compileShaderFile(frag_shader, SHADER_DIR "/main.frag"))
		THROW_ERROR(string("Failed to compile fragment shader: " SHADER_DIR "/main.frag\n") + infoLog);
	GLuint prog = glCreateProgram();
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
	GLint pLoc = glGetAttribLocation(prog, "p");
	GLint colorLoc = glGetUniformLocation(prog, "color");
	GLint centerLoc = glGetUniformLocation(prog, "center");
	GLint scaleLoc = glGetUniformLocation(prog, "scale");

	// setup camera
	float cX = 0.1075, cY = 0.9779;
	float scale = 2.4e4;

		// VBO
		GLuint VBO;
		glGenBuffers(1, &VBO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		GLsizei VBOsize = 0;
		for(const auto &mw : motorways) VBOsize += 2*(mw.size()-1);
		glBufferData(GL_ARRAY_BUFFER, VBOsize * 2 * sizeof(float), nullptr, GL_STATIC_DRAW);
		float *mapV = (float*) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		for(const auto &mw : motorways) {
			const int n = mw.size();
			for(int i = 1; i < n; ++i) {
				*(mapV++) = double(nodes[mw[i-1]].lon) * numbers::pi / 180e9;
				*(mapV++) = log(tan(numbers::pi * (double(nodes[mw[i-1]].lat) / 360e9 + .25)));
				*(mapV++) = double(nodes[mw[i]].lon) * numbers::pi / 180e9;
				*(mapV++) = log(tan(numbers::pi * (double(nodes[mw[i]].lat) / 360e9 + .25)));
			}
		}
		glUnmapBuffer(GL_ARRAY_BUFFER);

		// VAO
		GLuint VAO;
		glGenVertexArrays(1, &VAO);
		glBindVertexArray(VAO);
		glVertexAttribPointer(pLoc, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(pLoc);
		glBindVertexArray(0);

	while(!glfwWindowShouldClose(window)) {
		glClearColor(1.f, 1.f, 1.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);
		glUseProgram(prog);
		glUniform2f(centerLoc, cX, cY);
		glUniform2f(scaleLoc, scale/windowWidth, scale/windowHeight);

		glLineWidth(3.f);
		glUniform3f(colorLoc, 1.f, 0.f, 0.1f);
		glBindVertexArray(VAO);
		glDrawArrays(GL_LINES, 0, VBOsize);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}
	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}