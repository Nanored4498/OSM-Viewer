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

HashMap<Node> nodes;
vector<vector<int64_t>> motorways, departments;

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
	GLuint VBO;
	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	window.VBOsize = 0;
	for(const auto &mw : motorways) window.VBOsize += 2*(mw.size()-1);
	glBufferData(GL_ARRAY_BUFFER, window.VBOsize * 2 * sizeof(float), nullptr, GL_STATIC_DRAW);
	float *mapV = (float*) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	for(const auto &mw : motorways) {
		const int n = mw.size();
		for(int i = 1; i < n; ++i) {
			*(mapV++) = lon2Float(nodes[mw[i-1]].lon);
			*(mapV++) = lat2Float(nodes[mw[i-1]].lat);
			*(mapV++) = lon2Float(nodes[mw[i]].lon);
			*(mapV++) = lat2Float(nodes[mw[i]].lat);
		}
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);

	// VAO
	glGenVertexArrays(1, &window.VAO);
	glBindVertexArray(window.VAO);
	glVertexAttribPointer(window.pLoc, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(window.pLoc);
	glBindVertexArray(0);

	window.start();

	return 0;
}