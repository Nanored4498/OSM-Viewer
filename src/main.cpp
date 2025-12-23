// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
#include <numeric>
#include <ranges>

#include "triangulate.h"
#include "utils.h"
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

	// Get data
	OSMData data;
	data.read(argv[1]);

	// Triangulate multipolygons
	vector<uint32_t> forestIndices;
	vector<pair<uint32_t, uint32_t>> edgesA, edgesB;
	const auto vec2Comp = [&](const vec2i &a, const vec2i &b) {
		return a.x < b.x || (a.x == b.x && a.y < b.y);
	};
	const auto edgeComp = [&](const pair<uint32_t, uint32_t> &a, const pair<uint32_t, uint32_t> &b) {
		const vec2i &u = data.roads[a.first], &v = data.roads[b.first];
		if(u == v) [[unlikely]] return vec2Comp(data.roads[a.second], data.roads[b.second]);
		return vec2Comp(u, v);
	};
	for(const uint32_t i : views::iota(data.forestsR.first, data.forestsR.second)) {
		const span<uint32_t> refs(data.refs.data() + data.refOffsets[i], data.refs.data() + data.refOffsets[i+1]);
		size_t size = 0;
		for(const uint32_t j : refs) size += data.roadOffsets[j+1]-data.roadOffsets[j];
		unique_ptr<uint32_t[]> remap(new uint32_t[size + refs.size()]);
		uint32_t *const ends = remap.get() + size;
		bool out = true;
		edgesA.clear();
		edgesB.clear();
		for(auto j = refs.begin(); j != refs.end(); ++j) {
			// get a closed way
			uint32_t *m = remap.get() + (data.roadOffsets[*j+1] - data.roadOffsets[*j]);
			iota(remap.get(), m, data.roadOffsets[*j]);
			if(!data.isWayClosed(*j)) {
				while(data.roads[remap[0]] != data.roads[*(m-1)]) {
					if((++j) == refs.end()) THROW_ERROR("way not closed");
					uint32_t *const m0 = m;
					m += data.roadOffsets[*j+1] - data.roadOffsets[*j] - 1;
					if(data.roads[*(m0-1)] == data.roads[data.roadOffsets[*j]]) {
						iota(m0, m, data.roadOffsets[*j]+1);
					} else if(data.roads[*(m0-1)] == data.roads[data.roadOffsets[*j+1]-1]) {
						ranges::iota(span(m0, m) | views::reverse, data.roadOffsets[*j]);
					} else THROW_ERROR("way not closed");
				}
				--m;
			}

			// correct orientation
			int64_t area = 0;
			const int N = m-remap.get();
			for(int i = 0; i < N; ++i) {
				const vec2i &a = data.roads[remap[i]];
				const vec2i &b = data.roads[remap[(i+1)%N]];
				area += int64_t(a.x - b.x) * (a.y + b.y);
			}
			if(out != (area > 0)) reverse(remap.get(), m);
			out = false;

			// update graph
			for(int i = 0; i < N; ++i) {
				const uint32_t a = remap[i];
				const uint32_t b = remap[(i+1)%N];
				if(vec2Comp(data.roads[a], data.roads[b])) edgesA.emplace_back(a, b);
				else edgesB.emplace_back(b, a);
			}
		}

		ranges::sort(edgesA, edgeComp);
		ranges::sort(edgesB, edgeComp);
		auto itA = edgesA.begin(), itB = edgesB.begin();
		auto wA = itA, wB = itB;
		while(itA != edgesA.end() && itB != edgesB.end()) {
			const vec2i &u = data.roads[itA->first], &v = data.roads[itB->first];
			if(u != v) {
				if(vec2Comp(u, v)) *(wA++) = *(itA++);
				else *(wB++) = *(itB++);
				continue;
			}
			const vec2i &u2 = data.roads[itA->second], &v2 = data.roads[itB->second];
			if(u2 != v2) {
				if(vec2Comp(u2, v2)) *(wA++) = *(itA++);
				else *(wB++) = *(itB++);
				continue;
			}
			++ itA;
			++ itB;
		}
		wA = copy(itA, edgesA.end(), wA);
		wB = copy(itB, edgesB.end(), wB);
		edgesB.resize(wB - edgesB.begin());
		edgesA.resize((wA - edgesA.begin()) + edgesB.size());
		for(auto &[a, b] : edgesB) swap(a, b);
		ranges::sort(edgesB, edgeComp);
		wA = edgesA.end();
		itA = wA - edgesB.size();
		itB = edgesB.end();
		while(itA != edgesA.begin() && itB != edgesB.begin()) {
			const vec2i &u = data.roads[(itA-1)->first], &v = data.roads[(itB-1)->first];
			*(--wA) = vec2Comp(u, v) ? *(--itB) : *(--itA);
		}
		copy(edgesB.begin(), itB, edgesA.begin());
		bool bad = false;
		for(int j = 1; j < (int) edgesA.size(); ++j) {
			if(data.roads[edgesA[j-1].first] == data.roads[edgesA[j].first]) {
				bad = true;
				break;
			}
		}
		if(bad) {
			cerr << "touching holes: " << i - data.forestsR.first << ' ' << refs.size() << endl;
			continue;
		}

		uint32_t *e = ends;
		uint32_t *m = remap.get();
		uint32_t n_out = 0;
		for(auto &[a, b] : edgesA) {
			if(b == numeric_limits<uint32_t>::max()) continue;
			*(m++) = a;
			vec2i *u = data.roads.data() + b;
			b = numeric_limits<uint32_t>::max();
			const vec2i &v = data.roads[a];
			int64_t area = int64_t(v.x - u->x) * (v.y + u->y);
			while(*u != v) {
				auto it = ranges::lower_bound(edgesA, *u, vec2Comp, [&](const pair<uint32_t, uint32_t> &edge) {
					return data.roads[edge.first];
				});
				if(it == edgesA.end() || data.roads[it->first] != *u) THROW_ERROR("dsqf,sdjkg");
				vec2i* const u2 = data.roads.data() + it->second;
				area += int64_t(u->x - u2->x) * (u->y + u2->y);
				u = u2;
				it->second = numeric_limits<uint32_t>::max();
				*(m++) = it->first;
			}
			*(e++) = m - remap.get();
			if(area > 0) ++ n_out;
		}

		unique_ptr<vec2i[]> pts(new vec2i[m - remap.get()]);
		transform(remap.get(), m, pts.get(), [&](const uint32_t j) {
			return data.roads[j];
		});
		const vector<uint32_t> indices = triangulate(pts.get(), ends, e-ends, n_out);
		forestIndices.insert_range(forestIndices.end(), indices | views::transform([&](const uint32_t i) {
			return remap[i];
		}));
	}

	// Create window
	Window window;
	const auto mercator = [&](const vec2i &node)->vec2f {
		return vec2f(
			double(node.x) * (numbers::pi / 180e7),
			log(tan(numbers::pi * (double(node.y) / 360e7 + .25)))
		);
	};
	window.init(mercator(data.bbox.min), mercator(data.bbox.max));

	// Compute size
	GLsizei CMDcount = 0;
	const auto addRoads = [&](const auto &typeOff, const RoadStyle *styles) {
		for(uint32_t i = 0; i+1 < std::size(typeOff); ++i) {
			Window::Road &wr = window.roads.emplace_back();
			wr.col = styles[i].col;
			wr.col2 = styles[i].col2;
			wr.border = styles[i].border;
			wr.offset = (const void*) (CMDcount * sizeof(DrawCommand));
			CMDcount += (wr.count = typeOff[i+1] - typeOff[i]);
		}
	};
	addRoads(data.roadTypeOffsets, roadStyles);
	addRoads(data.waterWayTypeOffsets, waterWayStyles);
	{ // Country border
		Window::Road &wr = window.roads.emplace_back();
		wr.col = countryBorderColor;
		wr.border = false;
		wr.offset = (const void*) (CMDcount * sizeof(DrawCommand));
		CMDcount += (wr.count = data.boundaries.second - data.boundaries.first);
	}
	// Forests
	window.forestsCount =
		3 * (data.roadOffsets[data.forests.second] - data.roadOffsets[data.forests.first])
		- 6 * (data.forests.second - data.forests.first)
		+ forestIndices.size();
	// Capitals points
	window.capitalsFirst = data.roads.size();
	window.capitalsCount = data.capitals.size();

	// VBO, EBO, cmdBuffer
	GLuint VBO, EBO;
	glCreateBuffers(1, &VBO);
	glNamedBufferStorage(VBO, (window.capitalsFirst + window.capitalsCount) * sizeof(vec2f), nullptr, GL_MAP_WRITE_BIT);
	glCreateBuffers(1, &EBO);
	glNamedBufferStorage(EBO, window.forestsCount * sizeof(uint32_t), nullptr, GL_MAP_WRITE_BIT);
	glCreateBuffers(1, &window.cmdBuffer);
	glNamedBufferStorage(window.cmdBuffer, CMDcount * sizeof(DrawCommand), nullptr, GL_MAP_WRITE_BIT);
	vec2f* bufMap = (vec2f*) glMapNamedBuffer(VBO, GL_WRITE_ONLY);
	bufMap = ranges::transform(data.roads, bufMap, mercator).out;
	bufMap = ranges::transform(data.capitals, bufMap, [&](const auto &c) { return mercator(c.first); }).out;
	glUnmapNamedBuffer(VBO);
	DrawCommand *cmdMap = (DrawCommand*) glMapNamedBuffer(window.cmdBuffer, GL_WRITE_ONLY);
	for(uint32_t i = 0; i < data.boundaries.second; ++i) {
		cmdMap[i].count = data.roadOffsets[i+1] - data.roadOffsets[i];
		cmdMap[i].instanceCount = 1;
		cmdMap[i].first = data.roadOffsets[i];
		cmdMap[i].baseInstance = 0;
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
	indMap = ranges::copy(forestIndices, indMap).out;
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