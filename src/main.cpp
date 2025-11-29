// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
#include <ranges>

#include "triangulate.h"
#include "vec.h"
#include "window.h"

#include "data/data.h"

using namespace std;

struct RoadStyle {
	vec3f col, col2;
	bool border;
};

constexpr RoadStyle roadStyles[] {
	{{0.914f, 0.565f, 0.627f}, {0.878f, 0.180f, 0.420f}, true},
	{{0.988f, 0.753f, 0.675f}, {0.804f, 0.325f, 0.180f}, true},
	{{0.992f, 0.843f, 0.631f}, {0.671f, 0.482f, 0.012f}, false},
	{{0.667f, 0.827f, 0.875f}, {0.667f, 0.827f, 0.875f}, false},
};
constexpr RoadStyle waterWayStyles[] {
	{{0.667f, 0.827f, 0.875f}, {0.667f, 0.827f, 0.875f}, false},
};
constexpr vec3f countryBorderColor {0.812f, 0.608f, 0.796f};

int main(int argc, const char* argv[]) {
	if(argc != 2) {
		cerr << "Usage:\n";
		cerr << ">> " << argv[0] << " `map.osm.bin`\n";
		return 1;
	}

	OSMData data;
	data.read(argv[1]);

	// Create window
	Window window;
	const auto mercator = [&](const vec2l &node)->vec2f {
		return vec2f(
			double(node.x) * (numbers::pi / 180e9),
			log(tan(numbers::pi * (double(node.y) / 360e9 + .25)))
		);
	};
	window.init(mercator(data.bbox.min), mercator(data.bbox.max));

	// Compute size
	// TODO: VBOcount and CMDcount useless
	GLsizei VBOcount = 0, CMDcount = 0;
	const auto addRoads = [&](const auto &typeOff, const RoadStyle *styles) {
		for(uint32_t i = 0; i+1 < std::size(typeOff); ++i) {
			Window::Road &wr = window.roads.emplace_back();
			wr.col = styles[i].col;
			wr.col2 = styles[i].col2;
			wr.border = styles[i].border;
			wr.offset = (const void*) (CMDcount * 4 * sizeof(GLuint));
			CMDcount += (wr.count = typeOff[i+1] - typeOff[i]);
			VBOcount += data.roadOffsets[typeOff[i+1]] - data.roadOffsets[typeOff[i]];
		}
	};
	addRoads(data.roadTypeOffsets, roadStyles);
	addRoads(data.waterWayTypeOffsets, waterWayStyles);
	{ // Country border
		Window::Road &wr = window.roads.emplace_back();
		wr.col = countryBorderColor;
		wr.border = false;
		wr.offset = (const void*) (CMDcount * 4 * sizeof(GLuint));
		CMDcount += (wr.count = data.boundaries.second - data.boundaries.first);
		VBOcount += data.roadOffsets[data.boundaries.second] - data.roadOffsets[data.boundaries.first];
	}
	// Forests
	window.forestsCount = data.forests.second - data.forests.first;
	VBOcount += data.roadOffsets[data.forests.second] - data.roadOffsets[data.forests.first];
	cerr << VBOcount << ' ' << data.roads.size() << endl;
	// Capitals points
	window.capitalsFirst = VBOcount;
	VBOcount += (window.capitalsCount = data.capitals.size());

	// VBO, EBO, cmdBuffer
	GLuint VBO, EBO;
	glCreateBuffers(1, &VBO);
	glNamedBufferStorage(VBO, VBOcount * sizeof(vec2f), nullptr, GL_MAP_WRITE_BIT);
	glCreateBuffers(1, &EBO);
	glNamedBufferStorage(EBO, window.forestsCount * sizeof(uint32_t), nullptr, GL_MAP_WRITE_BIT);
	glCreateBuffers(1, &window.cmdBuffer);
	glNamedBufferStorage(window.cmdBuffer, CMDcount * 4 * sizeof(GLuint), nullptr, GL_MAP_WRITE_BIT);
	vec2f* bufMap = (vec2f*) glMapNamedBuffer(VBO, GL_WRITE_ONLY);
	bufMap = ranges::transform(data.roads, bufMap, mercator).out;
	bufMap = ranges::transform(data.capitals, bufMap, [&](const auto &c) { return mercator(c.first); }).out;
	glUnmapNamedBuffer(VBO);
	GLuint *cmdMap = (GLuint*) glMapNamedBuffer(window.cmdBuffer, GL_WRITE_ONLY);
	for(uint32_t i = 0; i < data.boundaries.second; ++i) {
		*(cmdMap++) = data.roadOffsets[i+1] - data.roadOffsets[i]; // count
		*(cmdMap++) = 1; // instanceCount
		*(cmdMap++) = data.roadOffsets[i]; // first
		*(cmdMap++) = 0; // baseInstance
	}
	glUnmapNamedBuffer(window.cmdBuffer);
	GLuint *indMap = (GLuint*) glMapNamedBuffer(EBO, GL_WRITE_ONLY);
	for(uint32_t i = data.forests.first; i < data.forests.second; ++i) {
		const vector<uint32_t> indices = triangulate(
			data.roads.data() + data.roadOffsets[i],
			data.roadOffsets[i+1]-data.roadOffsets[i]
		);
		indMap = ranges::transform(indices, indMap, [&](uint32_t x) { return x + data.roadOffsets[i]; }).out;
	}
	glUnmapNamedBuffer(EBO);

	// VAO
	glCreateVertexArrays(1, &window.VAO);
	glVertexArrayVertexBuffer(window.VAO, 0, VBO, 0, sizeof(vec2f));
	glVertexArrayElementBuffer(window.VAO, EBO);
	window.progs.main.bind_p(window.VAO, 0, 0);

	// Text VBO
	window.charactersCount = 0;
	window.framesCount = data.roadNames.size();
	for(auto txts : {&data.capitals, &data.roadNames})
	for(const uint32_t &id : *txts | views::elements<1>)
		window.charactersCount += strlen(data.names.data()+id);

	GLuint textVBO;
	glCreateBuffers(1, &textVBO);
	glNamedBufferStorage(textVBO,
			window.charactersCount * sizeof(Programs::Text::Attribs)
			+ window.framesCount * sizeof(Programs::Frame::Attribs),
			nullptr, GL_MAP_WRITE_BIT);
	Programs::Text::Attribs *txtMap = (decltype(txtMap)) glMapNamedBuffer(textVBO, GL_WRITE_ONLY);
	Programs::Frame::Attribs *frmMap = (decltype(frmMap)) (txtMap + window.charactersCount);
	for(auto txts : {&data.capitals, &data.roadNames}) {
		const Font::CharPositions &cps = txts == &data.capitals ? window.capitalFont : window.roadFont;
		for(const auto &[pt, id] : *txts) {
			if(!data.names[id]) continue;
			const vec2f txtCenter = mercator(pt);
			const string_view name(data.names.data()+id);
			vec2f offset(0.f, numeric_limits<float>::max());
			float y1 = numeric_limits<float>::min();
			for(int c : name) {
				const auto &cp = cps[c - Font::firstChar];
				offset.x += cp.xadvance;
				offset.y = min(offset.y, cp.yoff);
				y1 = max(y1, cp.yoff + cp.y1 - cp.y0);
			}
			const auto &cp0 = cps[name[0] - Font::firstChar];
			const auto &cp1 = cps[name.back() - Font::firstChar];
			const float x0 = cp0.xoff;
			const float x1 = offset.x - cp1.xadvance + cp1.xoff + cp1.x1 - cp1.x0;
			offset.x = - (x0 + x1) / 2.f;
			if(txts == &data.capitals) offset.y -= 6.f;
			if(txts == &data.roadNames) {
				constexpr float margin = 4.f;
				frmMap->txtCenter = txtCenter;
				frmMap->offset = offset + vec2f(x0-margin, -y1-margin);
				frmMap->size.x = x1 - x0 + 2.f*margin;
				frmMap->size.y = y1 - offset.y + 2.f*margin;
				++frmMap;
			}
			for(int c : name) {
				const auto &cp = cps[c - Font::firstChar];
				txtMap->txtCenter = txtCenter;
				txtMap->offset = offset + vec2f(cp.xoff, -cp.yoff);
				txtMap->size.x = cp.x1 - cp.x0;
				txtMap->size.y = cp.y0 - cp.y1;
				txtMap->uv.x = (float) cp.x0 / window.atlas.width;
				txtMap->uv.y = (float) cp.y0 / window.atlas.height;
				txtMap->uvSize.x = float(cp.x1 - cp.x0) / window.atlas.width;
				txtMap->uvSize.y = float(cp.y1 - cp.y0) / window.atlas.height;
				txtMap->color = txts == &data.capitals ? vec3f(0.f, 0.f, 0.f) : vec3f(1.f, 1.f, 1.f);
				++ txtMap;
				offset.x += cp.xadvance;
			}
		}
	}
	glUnmapNamedBuffer(textVBO);

	// Text VAO
	glCreateVertexArrays(1, &window.textVAO);
	glVertexArrayVertexBuffer(window.textVAO, 0, textVBO, 0, sizeof(Programs::Text::Attribs));
	glVertexArrayBindingDivisor(window.textVAO, 0, 1);
	window.progs.text.canonical_bind(window.textVAO, 0);
	glCreateVertexArrays(1, &window.frameVAO);
	glVertexArrayVertexBuffer(window.frameVAO, 0, textVBO, window.charactersCount * sizeof(Programs::Text::Attribs), sizeof(Programs::Frame::Attribs));
	glVertexArrayBindingDivisor(window.frameVAO, 0, 1);
	window.progs.frame.canonical_bind(window.frameVAO, 0);

	window.start();

	return 0;
}