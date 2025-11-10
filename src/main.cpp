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
#include <ranges>
#include <unordered_set>

// TODO: rewrite decompression
#include <zlib.h>

#include "hashmap.h"
#include "utils.h"
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

struct Node {
	int64_t lon, lat;
};

struct Color {
	float r, g, b;
};

struct RoadType {
	string name;
	Color col, col2;
	bool border;
};

HashMap<Node> nodes;

constexpr RoadType roadTypes[] {
	{"motorway", {0.914f, 0.565f, 0.627f}, {0.878f, 0.180f, 0.420f}, true},
	{"trunk",    {0.988f, 0.753f, 0.675f}, {0.804f, 0.325f, 0.180f}, true},
	{"primary",  {0.992f, 0.843f, 0.631f}, {0.671f, 0.482f, 0.012f}, false},
	{"river",    {0.667f, 0.827f, 0.875f}, {0.667f, 0.827f, 0.875f}, false},
};
vector<pair<int64_t, vector<int64_t>>> roads[std::size(roadTypes)], countryBorders;
vector<pair<string, int64_t>> capitals;
vector<int64_t> mainRivers;

constexpr Color countryBorderColor {0.812f, 0.608f, 0.796f};

int main() {
	BinStream input("map/lorraine-latest.osm.pbf");
	uint32_t blobHeaderSize;
	vector<uint8_t> wire;
	bool hasHeader = false;
	Proto::HeaderBBox bbox;
	bbox.right = numeric_limits<int64_t>::min();
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
			if(hb._has_bbox) bbox = hb.bbox;
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
					bool is_boundary = false;
					int admin_level = -1;
					for(int i = 0; i < T; ++i) {
						const string key(ST[way.keys[i]].begin(), ST[way.keys[i]].end());
						const string val(ST[way.vals[i]].begin(), ST[way.vals[i]].end());
						if(key == "highway" || key == "waterway") {
							const int i = ranges::find(roadTypes, val, [&](const RoadType &rt) {
								return rt.name;
							}) - roadTypes;
							if(i < (int) std::size(roadTypes))
								roads[i].emplace_back(way.id, way.refs);
						} else if(key == "boundary") {
							if(val == "administrative")
								is_boundary = true;
						} else if(key == "admin_level") {
							admin_level = stoi(val);
						}
					}
					if(is_boundary && admin_level == 2) {
						countryBorders.emplace_back(way.id, way.refs);
					}
				}
				for(const Proto::Relation &relation : pg.relations) {
					const int T = relation.keys.size();
					if(T != (int) relation.vals.size()) {
						cerr << "Sizes mismatch in relation's tags... (" << __FILE__ << ':' << __LINE__ << ")\n";
						exit(1);
					}
					const int M = relation.memids.size();
					if(M != (int) relation.roles_sid.size()) {
						cerr << "Sizes mismatch in relation's members... (" << __FILE__ << ':' << __LINE__ << ")\n";
						exit(1);
					}
					if(M != (int) relation.types.size()) {
						cerr << "Sizes mismatch in relation's members... (" << __FILE__ << ':' << __LINE__ << ")\n";
						exit(1);
					}
					bool isWaterway = false, isRiver = false;
					string name, sandre = "none...";
					for(int i = 0; i < T; ++i) {
						const string key(ST[relation.keys[i]].begin(), ST[relation.keys[i]].end());
						const string val(ST[relation.vals[i]].begin(), ST[relation.vals[i]].end());
						if(key == "type") {
							if(val == "waterway") isWaterway = true;
						} else if(key == "waterway") {
							if(val == "river") isRiver = true;
						} else if(key == "name") {
							name = val;
						} else if(key == "ref:sandre") {
							sandre = val;
						}
					}
					if(isWaterway && isRiver && sandre.size() > 1 && sandre[1] == '-') {
						int64_t memid = 0;
						for(int i = 0; i < M; ++i) {
							memid += relation.memids[i];
							if(relation.types[i] != Proto::Relation::MemberType::WAY) continue;
							const string role(ST[relation.roles_sid[i]].begin(), ST[relation.roles_sid[i]].end());
							if(role != "main_stream") continue;
							mainRivers.push_back(memid);
						}
					}
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
						string place, name;
						int capital = -1;
						while(*kv_it) {
							const string key(ST[*kv_it].begin(), ST[*kv_it].end());
							++ kv_it;
							const string val(ST[*kv_it].begin(), ST[*kv_it].end());
							++ kv_it;
							if(key == "place") {
								place = val;
							} else if(key == "name") {
								name = val;
							} else if(key == "capital") {
								if(val == "yes") capital = 2;
								else capital = stoi(val);
							}
						}
						if(place == "city") {
							if(capital >= 0 && capital <= 6)
								capitals.emplace_back(name, id);
						}
						++ kv_it;
					}
					if(kv_it != dense.keys_vals.end()) {
						cerr << "Sizes mismatch in denseNodes... (" << __FILE__ << ':' << __LINE__ << ")\n";
						exit(1);
					}
				}
			}
		} else {
			cerr << "Not recognized blob type: " << header.type << endl;
			exit(1);
		}
	}

	{ // Only keep main rivers
		auto &rivers = roads[ranges::find(roadTypes, "river", [&](const RoadType &rt) {
			return rt.name;
		}) - roadTypes];
		ranges::sort(rivers, less<int64_t>{},
			[](const decltype(rivers[0]) &r) {
				return r.first;
			}
		);
		ranges::sort(mainRivers);
		auto good = mainRivers.begin();
		int n = 0;
		for(auto &r : rivers) {
			while(good != mainRivers.end() && *good < r.first)
				++ good;
			if(good == mainRivers.end()) break;
			if(r.first != *good) continue;
			rivers[n++] = std::move(r);
		}
		cerr << n << endl;
		rivers.resize(n);
	}

	if(bbox.right == numeric_limits<int64_t>::min()) {
		bbox.left = numeric_limits<int64_t>::max();
		bbox.top = numeric_limits<int64_t>::min();
		bbox.bottom = numeric_limits<int64_t>::max();
		for(const auto &[id, node] : nodes) {
			bbox.left   = min(bbox.left,   node.lon);
			bbox.right  = max(bbox.right,  node.lon);
			bbox.bottom = min(bbox.bottom, node.lat);
			bbox.top    = max(bbox.top,    node.lat);
		}
	}

	const auto lon2Float = [](int64_t x) {
		return double(x) * (numbers::pi / 180e9);
	};
	const auto lat2Float = [](int64_t x) {
		return log(tan(numbers::pi * (double(x) / 360e9 + .25)));
	};

	Window window;
	window.init(lon2Float(bbox.left), lon2Float(bbox.right), lat2Float(bbox.bottom), lat2Float(bbox.top));

	// VBO

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
		CMDcount += (wr.count = roads[i].size());
		for(const auto &r : roads[i] | views::elements<1>) VBOcount += r.size();
	}
	{ // Country border
		Window::Road &wr = window.roads.emplace_back();
		wr.r = countryBorderColor.r;
		wr.g = countryBorderColor.g;
		wr.b = countryBorderColor.b;
		wr.border = false;
		wr.offset = (const void*) (CMDcount * 4 * sizeof(GLuint));
		CMDcount += (wr.count = countryBorders.size());
		for(const auto &r : countryBorders | views::elements<1>) VBOcount += r.size();
	}
	// Capitals points
	window.capitalsFirst = VBOcount;
	window.capitalsCount = capitals.size();
	VBOcount += capitals.size();

	// VBO and cmdBuffer
	GLuint VBO;
	glCreateBuffers(1, &VBO);
	glNamedBufferStorage(VBO, VBOcount * 2 * sizeof(float), nullptr, GL_MAP_WRITE_BIT);
	glCreateBuffers(1, &window.cmdBuffer);
	glNamedBufferStorage(window.cmdBuffer, CMDcount * 4 * sizeof(GLuint), nullptr, GL_MAP_WRITE_BIT);
	float *bufMap = (float*) glMapNamedBuffer(VBO, GL_WRITE_ONLY);
	GLuint *cmdMap = (GLuint*) glMapNamedBuffer(window.cmdBuffer, GL_WRITE_ONLY);
	VBOcount = 0;
	const auto writeRoads2GPU = [&](const vector<pair<int64_t, vector<int64_t>>> &roads) {
		for(const auto &r : roads | views::elements<1>) {
			*(cmdMap++) = r.size(); // count
			*(cmdMap++) = 1; // instanceCount
			*(cmdMap++) = VBOcount; // first
			*(cmdMap++) = 0; // baseInstance
			for(const int64_t id : r) {
				*(bufMap++) = lon2Float(nodes[id].lon);
				*(bufMap++) = lat2Float(nodes[id].lat);
			}
			VBOcount += r.size();
		}
	};
	for(const auto &roads : roads) writeRoads2GPU(roads);
	writeRoads2GPU(countryBorders);
	for(const int64_t id : capitals | views::elements<1>) {
		*(bufMap++) = lon2Float(nodes[id].lon);
		*(bufMap++) = lat2Float(nodes[id].lat);
	}
	glUnmapNamedBuffer(VBO);
	glUnmapNamedBuffer(window.cmdBuffer);

	// VAO
	glCreateVertexArrays(1, &window.VAO);
	glVertexArrayVertexBuffer(window.VAO, 0, VBO, 0, 2 * sizeof(float));
	window.progs.main.bind_p(window.VAO, 0, 0);

	// Capitals text SSBO
	glCreateBuffers(1, &window.textSSBO);
	window.charactersCount = 0;
	for(const string &name : capitals | views::elements<0>)
		window.charactersCount += name.size();
	glNamedBufferStorage(window.textSSBO, window.charactersCount * 10 * sizeof(float), nullptr, GL_MAP_WRITE_BIT);
	bufMap = (float*) glMapNamedBuffer(window.textSSBO, GL_WRITE_ONLY);
	for(const auto &[name, id] : capitals) {
		if(name.empty()) continue;
		const float txtCenterX = lon2Float(nodes[id].lon);
		const float txtCenterY = lat2Float(nodes[id].lat);
		float offX = 0.f, offY = 1e9f;
		for(int c : name) {
			const auto &pc = window.atlas.charPositions[c - Font::firstChar];
			offY = min(offY, pc.yoff);
			offX += pc.xadvance;
		}
		const auto &pc0 = window.atlas.charPositions[name[0] - Font::firstChar];
		const auto &pc1 = window.atlas.charPositions[name.back() - Font::firstChar];
		offX = - (pc0.xoff + offX - pc1.xadvance + pc1.xoff + pc1.x1 - pc1.x0) / 2.f;
		offY -= 6.f;
		for(int c : name) {
			const auto &cp = window.atlas.charPositions[c - Font::firstChar];
			*(bufMap++) = txtCenterX;
			*(bufMap++) = txtCenterY;
			*(bufMap++) = offX + cp.xoff;
			*(bufMap++) = offY - cp.yoff;
			*(bufMap++) = cp.x1 - cp.x0;
			*(bufMap++) = cp.y0 - cp.y1;
			*(bufMap++) = (float) cp.x0 / window.atlas.width;
			*(bufMap++) = (float) cp.y0 / window.atlas.height;
			*(bufMap++) = float(cp.x1 - cp.x0) / window.atlas.width;
			*(bufMap++) = float(cp.y1 - cp.y0) / window.atlas.height;
			offX += cp.xadvance;
		}
	}
	glUnmapNamedBuffer(window.textSSBO);

	window.start();

	return 0;
}