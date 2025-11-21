// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numbers>
#include <ranges>
#include <string_view>
#include <unordered_set>

// TODO: rewrite decompression
#include <zlib.h>

#include "hashmap.h"
#include "triangulate.h"
#include "utils.h"
#include "vec.h"
#include "window.h"

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

struct Color {
	float r, g, b;
};

struct RoadType {
	string name;
	Color col, col2;
	bool border;
};

HashMap<vec2l> nodes;

constexpr RoadType roadTypes[] {
	{"motorway", {0.914f, 0.565f, 0.627f}, {0.878f, 0.180f, 0.420f}, true},
	{"trunk",    {0.988f, 0.753f, 0.675f}, {0.804f, 0.325f, 0.180f}, true},
	{"primary",  {0.992f, 0.843f, 0.631f}, {0.671f, 0.482f, 0.012f}, false},
	{"river",    {0.667f, 0.827f, 0.875f}, {0.667f, 0.827f, 0.875f}, false},
};
HashMap<vector<int64_t>> roads[std::size(roadTypes)], countryBorders;
vector<pair<string, int64_t>> capitals, mainRoads;
vector<int64_t> mainRivers;

vector<vector<int64_t>> forests;

constexpr Color countryBorderColor {0.812f, 0.608f, 0.796f};

consteval HashMap<vector<int64_t>>& getRoadsByName(const string &name) {
	return roads[ranges::find(roadTypes, name, [&](const RoadType &rt) {
			return rt.name;
	}) - roadTypes];
}
constexpr auto &motorways = getRoadsByName("motorway");
constexpr auto &rivers = getRoadsByName("river");

void addMainRoad(const string_view &name, const vector<int64_t> &mems) {
	for(const int64_t mem : mems) {
		auto it = motorways.find(mem);
		if(it == motorways.end()) continue;
		mainRoads.emplace_back(name, it->second[0]);
		break;
	}
}

int main() {
	BinStream input(MAP_DIR "/lorraine-latest.osm.pbf");
	uint32_t blobHeaderSize;
	vector<uint8_t> wire;
	bool hasHeader = false;
	Box<vec2l> bbox;
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
			if(uncompress(data.data(), &data_size, blob.data.zlib_data.data(), blob.data.zlib_data.size()) != Z_OK)
				THROW_ERROR("Failed to uncompress...");
		} else THROW_ERROR("Uncompression of blob data not implemented " + to_string(blob._data_choice));
		if(header.type == "OSMHeader") {
			if(hasHeader) THROW_ERROR("multiple OSMHeader...");
			hasHeader = true;
			const Proto::HeaderBlock hb(data);
			if(hb._has_bbox) {
				bbox.min.x = hb.bbox.left;
				bbox.min.y = hb.bbox.bottom;
				bbox.max.x = hb.bbox.right;
				bbox.max.y = hb.bbox.top;
			}
			for(const string &feature : hb.required_features) {
				if(!supported_features.count(feature))
					THROW_ERROR("Not supported required feature: " + feature);
			}
			cout << "Optional features:\n";
			for(const string &s : hb.optional_features)
				cout << '\t' << s << endl;
		} else if(header.type == "OSMData") {
			if(!hasHeader) THROW_ERROR("OSMData blob before any OSMHeader...");
			Proto::PrimitiveBlock pb(data);
			const auto &ST = pb.stringtable.s;
			const auto getString = [&](uint32_t i) {
				return string_view(reinterpret_cast<const char*>(ST[i].data()), ST[i].size());
			};
			for(Proto::PrimitiveGroup &pg : pb.primitivegroup) {
				if(!pg.nodes.empty()) THROW_ERROR("Not implemented");
				if(!pg.changesets.empty()) THROW_ERROR("Not implemented");
				for(Proto::Way &way : pg.ways) {
					if(!way.lat.empty() || !way.lon.empty()) THROW_ERROR("lat and lon fields in Way are not supported");
					const int T = way.keys.size();
					if(T != (int) way.vals.size()) THROW_ERROR("Sizes mismatch in way's tags...");
					const int R = way.refs.size();
					for(int i = 1; i < R; ++i)
						way.refs[i] += way.refs[i-1];
					bool is_boundary = false;
					int admin_level = -1;
					bool is_forest = false;
					for(int i = 0; i < T; ++i) {
						const string_view key = getString(way.keys[i]);
						const string_view val = getString(way.vals[i]);
						if(key == "highway" || key == "waterway") {
							const ptrdiff_t i = ranges::find(roadTypes, val, [&](const RoadType &rt) {
								return rt.name;
							}) - roadTypes;
							if(i < (ptrdiff_t) std::size(roadTypes))
								roads[i][way.id] = move(way.refs);
						} else if(key == "boundary") {
							if(val == "administrative")
								is_boundary = true;
						} else if(key == "admin_level") {
							if(from_chars(val.begin(), val.end(), admin_level).ec != errc())
								THROW_ERROR("admin_level is not a number: " + string(val));
						} else if(key == "landuse") {
							if(val == "forest")
								is_forest = true;
						} else if(key == "natural") {
							if(val == "wood")
								is_forest = true;
						}
					}
					if(is_boundary && admin_level != -1 && admin_level <= 4) {
						// TODO: different rendering depending on admin level
						countryBorders[way.id] = move(way.refs);
					} else if(is_forest) {
						if(way.refs[0] != way.refs.back()) THROW_ERROR("Not closed");
						way.refs.pop_back();
						forests.emplace_back(move(way.refs));
					}
				}
				for(const Proto::Relation &relation : pg.relations) {
					const int T = relation.keys.size();
					if(T != (int) relation.vals.size()) THROW_ERROR("Sizes mismatch in relation's tags...");
					const int M = relation.memids.size();
					if(M != (int) relation.roles_sid.size()) THROW_ERROR("Sizes mismatch in relation's members...");
					if(M != (int) relation.types.size()) THROW_ERROR("Sizes mismatch in relation's members...");
					bool isWaterway = false, isRiver = false;
					bool isRoute = false;
					string_view sandre;
					string_view network, ref;
					for(int i = 0; i < T; ++i) {
						const string_view key = getString(relation.keys[i]);
						const string_view val = getString(relation.vals[i]);
						if(key == "type") {
							if(val == "waterway") isWaterway = true;
							else if(val == "route") isRoute = true;
						} else if(key == "waterway") {
							if(val == "river") isRiver = true;
						} else if(key == "ref:sandre") {
							sandre = val;
						} else if(key == "network") {
							network = val;
						} else if(key == "ref") {
							ref = val;
						}
					}
					if(isWaterway && isRiver && sandre.size() > 1 && sandre[1] == '-') {
						int64_t memid = 0;
						for(int i = 0; i < M; ++i) {
							memid += relation.memids[i];
							if(relation.types[i] != Proto::Relation::MemberType::WAY) continue;
							const string_view role = getString(relation.roles_sid[i]);
							if(role == "main_stream" && rivers.contains(memid))
								mainRivers.push_back(memid);
						}
					}
					if(isRoute && (network == "FR:A-road" || network == "FR:N-road"))
						addMainRoad(ref, relation.memids);
				}
				if(pg._has_dense) {
					const Proto::DenseNodes &dense = pg.dense;
					const int N = dense.id.size();
					if(N != (int) dense.lat.size() || N != (int) dense.lon.size())
						THROW_ERROR("Sizes mismatch in denseNodes...");
					auto kv_it = dense.keys_vals.begin();
					int64_t id = 0, lat = 0, lon = 0;
					for(int i = 0; i < N; ++i) {
						id += dense.id[i];
						lat += dense.lat[i];
						lon += dense.lon[i];
						vec2l &node = nodes[id];
						node.x = pb.lon_offset + pb.granularity * lon;
						node.y = pb.lat_offset + pb.granularity * lat;
						string_view place, name;
						int capital = -1;
						while(*kv_it) {
							const string_view key = getString(*(kv_it++));
							const string_view val = getString(*(kv_it++));
							if(key == "place") {
								place = val;
							} else if(key == "name") {
								name = val;
							} else if(key == "capital") {
								if(val == "yes") capital = 2;
								else if(from_chars(val.begin(), val.end(), capital).ec != errc())
									THROW_ERROR("admin_level is not a number: " + string(val));
							}
						}
						if(place == "city") {
							if(capital >= 0 && capital <= 6)
								capitals.emplace_back(name, id);
						}
						++ kv_it;
					}
					if(kv_it != dense.keys_vals.end()) THROW_ERROR("Sizes mismatch in denseNodes...");
				}
			}
		} else THROW_ERROR("Not recognized blob type: " + header.type);
	}

	// Create window
	Window window;
	if(bbox.min.x == numeric_limits<int64_t>::max())
		for(const vec2l &node : nodes | views::values)
			bbox.update(node);
	const auto mercator = [&](const vec2l &node)->vec2f {
		return vec2f(
			double(node.x) * (numbers::pi / 180e9),
			log(tan(numbers::pi * (double(node.y) / 360e9 + .25)))
		);
	};
	window.init(mercator(bbox.min), mercator(bbox.max));

	// Compute size
	GLsizei VBOcount = 0, CMDcount = 0;
	for(int i = 0; i < (int) std::size(roads); ++i) {
		Window::Road &wr = window.roads.emplace_back();
		wr.r = roadTypes[i].col.r;
		wr.g = roadTypes[i].col.g;
		wr.b = roadTypes[i].col.b;
		wr.r2 = roadTypes[i].col2.r;
		wr.g2 = roadTypes[i].col2.g;
		wr.b2 = roadTypes[i].col2.b;
		wr.border = roadTypes[i].border;
		wr.offset = (const void*) (CMDcount * 4 * sizeof(GLuint));
		if(&roads[i] == &rivers) {
			CMDcount += (wr.count = mainRivers.size());
			for(int64_t rid : mainRivers) VBOcount += roads[i][rid].size();
		} else {
			CMDcount += (wr.count = roads[i].size());
			for(const auto &r : roads[i] | views::values) VBOcount += r.size();
		}
	}
	{ // Country border
		Window::Road &wr = window.roads.emplace_back();
		wr.r = countryBorderColor.r;
		wr.g = countryBorderColor.g;
		wr.b = countryBorderColor.b;
		wr.border = false;
		wr.offset = (const void*) (CMDcount * 4 * sizeof(GLuint));
		CMDcount += (wr.count = countryBorders.size());
		for(const auto &r : countryBorders | views::values) VBOcount += r.size();
	}
	// Capitals points
	window.capitalsFirst = VBOcount;
	VBOcount += (window.capitalsCount = capitals.size());
	// Forests
	uint32_t forestsFirst = VBOcount;
	window.forestsCount = 0;
	for(const auto &forest : forests) {
		if(forest.size() < 3) THROW_ERROR("area with less than 3 nodes");
		VBOcount += forest.size();
		window.forestsCount += 3 * (forest.size() - 2);
	}

	// VBO, EBO, cmdBuffer
	GLuint VBO, EBO;
	glCreateBuffers(1, &VBO);
	glNamedBufferStorage(VBO, VBOcount * sizeof(vec2f), nullptr, GL_MAP_WRITE_BIT);
	glCreateBuffers(1, &EBO);
	glNamedBufferStorage(EBO, window.forestsCount * sizeof(uint32_t), nullptr, GL_MAP_WRITE_BIT);
	glCreateBuffers(1, &window.cmdBuffer);
	glNamedBufferStorage(window.cmdBuffer, CMDcount * 4 * sizeof(GLuint), nullptr, GL_MAP_WRITE_BIT);
	vec2f *bufMap = (vec2f*) glMapNamedBuffer(VBO, GL_WRITE_ONLY);
	GLuint *indMap = (GLuint*) glMapNamedBuffer(EBO, GL_WRITE_ONLY);
	GLuint *cmdMap = (GLuint*) glMapNamedBuffer(window.cmdBuffer, GL_WRITE_ONLY);
	VBOcount = 0;
	const auto writeRoad2GPU = [&](const vector<int64_t> &road) {
		*(cmdMap++) = road.size(); // count
		*(cmdMap++) = 1; // instanceCount
		*(cmdMap++) = VBOcount; // first
		*(cmdMap++) = 0; // baseInstance
		for(const int64_t id : road)
			*(bufMap++) = mercator(nodes[id]);
		VBOcount += road.size();
	};
	for(const auto &roads : roads) {
		if(&roads == &rivers) {
			for(const int64_t &rid : mainRivers)
				writeRoad2GPU(roads.find(rid)->second);
		} else {
			for(const auto &road : roads | views::values)
				writeRoad2GPU(road);
		}
	}
	for(const auto &road : countryBorders | views::values)
		writeRoad2GPU(road);
	for(const int64_t id : capitals | views::elements<1>)
		*(bufMap++) = mercator(nodes[id]);
	for(const auto &forest : forests) {
		vector<vec2l> pts;
		pts.reserve(forest.size());
		for(const int64_t id : forest)
			*(bufMap++) = mercator(pts.emplace_back(nodes[id]));
		vector<uint32_t> indices = triangulate(pts);
		for(uint32_t i : indices)
			*(indMap++) = forestsFirst + i;
		forestsFirst += forest.size();
	}
	glUnmapNamedBuffer(VBO);
	glUnmapNamedBuffer(EBO);
	glUnmapNamedBuffer(window.cmdBuffer);

	// VAO
	glCreateVertexArrays(1, &window.VAO);
	glVertexArrayVertexBuffer(window.VAO, 0, VBO, 0, sizeof(vec2f));
	glVertexArrayElementBuffer(window.VAO, EBO);
	window.progs.main.bind_p(window.VAO, 0, 0);

	// Text VBO, VAO and frames SSBO
	window.charactersCount = 0;
	window.framesCount = mainRoads.size();
	for(auto txts : {&capitals, &mainRoads})
	for(const string &name : *txts | views::elements<0>)
		window.charactersCount += name.size();

	GLuint textVBO;
	glCreateBuffers(1, &textVBO);
	glNamedBufferStorage(textVBO, window.charactersCount * 5 * sizeof(vec2f), nullptr, GL_MAP_WRITE_BIT);
	glCreateBuffers(1, &window.frameSSBO);
	glNamedBufferStorage(window.frameSSBO, window.framesCount * 3 * sizeof(vec2f), nullptr, GL_MAP_WRITE_BIT);
	bufMap = (vec2f*) glMapNamedBuffer(textVBO, GL_WRITE_ONLY);
	vec2f* frameMap = (vec2f*) glMapNamedBuffer(window.frameSSBO, GL_WRITE_ONLY);
	for(auto txts : {&capitals, &mainRoads})
	for(const auto &[name, id] : *txts) {
		if(name.empty()) continue;
		const vec2f txtCenter = mercator(nodes[id]);
		vec2f offset(0.f, numeric_limits<float>::max());
		float y1 = numeric_limits<float>::min();
		for(int c : name) {
			const auto &pc = window.atlas.charPositions[c - Font::firstChar];
			offset.x += pc.xadvance;
			offset.y = min(offset.y, pc.yoff);
			y1 = max(y1, pc.yoff + pc.y1 - pc.y0);
		}
		const auto &cp0 = window.atlas.charPositions[name[0] - Font::firstChar];
		const auto &cp1 = window.atlas.charPositions[name.back() - Font::firstChar];
		const float x0 = cp0.xoff;
		const float x1 = offset.x - cp1.xadvance + cp1.xoff + cp1.x1 - cp1.x0;
		offset.x = - (x0 + x1) / 2.f;
		if(txts == &capitals) offset.y -= 6.f;
		if(txts == &mainRoads) {
			constexpr float margin = 6.f;
			*(frameMap++) = txtCenter;
			*(frameMap++) = offset + vec2f(x0-margin, -y1-margin);
			frameMap->x     = x1 - x0 + 2.f*margin;
			(frameMap++)->y = y1 - offset.y + 2.f*margin;
		}
		for(int c : name) {
			const auto &cp = window.atlas.charPositions[c - Font::firstChar];
			*(bufMap++) = txtCenter;
			*(bufMap++) = offset + vec2f(cp.xoff, -cp.yoff);
			bufMap->x     = cp.x1 - cp.x0;
			(bufMap++)->y = cp.y0 - cp.y1;
			bufMap->x     = (float) cp.x0 / window.atlas.width;
			(bufMap++)->y = (float) cp.y0 / window.atlas.height;
			bufMap->x     = float(cp.x1 - cp.x0) / window.atlas.width;
			(bufMap++)->y = float(cp.y1 - cp.y0) / window.atlas.height;
			offset.x += cp.xadvance;
		}
	}
	glUnmapNamedBuffer(textVBO);
	glUnmapNamedBuffer(window.frameSSBO);

	glCreateVertexArrays(1, &window.textVAO);
	glVertexArrayVertexBuffer(window.textVAO, 0, textVBO, 0, 5 * sizeof(vec2f));
	glVertexArrayBindingDivisor(window.textVAO, 0, 1);
	window.progs.text.bind_txtCenter(window.textVAO, 0, 0);
	window.progs.text.bind_offset(window.textVAO, 0, 1 * sizeof(vec2f));
	window.progs.text.bind_size(window.textVAO, 0, 2 * sizeof(vec2f));
	window.progs.text.bind_uv(window.textVAO, 0, 3 * sizeof(vec2f));
	window.progs.text.bind_uvSize(window.textVAO, 0, 4 * sizeof(vec2f));

	window.start();

	return 0;
}