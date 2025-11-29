// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <concepts>
#include <fstream>
#include <iostream>
#include <limits>
#include <ranges>
#include <unordered_set>

// TODO: rewrite decompression
#include <zlib.h>

#include "string_switch.h"

#include "hashmap.h"
#include "utils.h"
#include "vec.h"

#include "data/data.h"

#include "enums/enums.h"

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

static void readHeader(const vector<uint8_t> &blobData, OSMData &data) {
	const Proto::HeaderBlock hb(blobData);
	if(hb._has_bbox) {
		data.bbox.min.x = hb.bbox.left;
		data.bbox.min.y = hb.bbox.bottom;
		data.bbox.max.x = hb.bbox.right;
		data.bbox.max.y = hb.bbox.top;
	}
	for(const string &feature : hb.required_features) {
		if(!supported_features.count(feature))
			THROW_ERROR("Not supported required feature: " + feature);
	}
	if(hb.optional_features.empty()) return;
	cout << "Optional features:\n";
	for(const string &s : hb.optional_features)
		cout << '\t' << s << endl;
}

struct TmpRoad {
	vector<vec2l> pts;
	vector<uint32_t> off = {0};
	void end() {
		off.push_back(pts.size());
	}
};

struct TmpData {
	array<TmpRoad, (size_t) RoadType::NUM> roads;
	array<TmpRoad, (size_t) WaterWayType::NUM> waterWays;
	TmpRoad boundaries, forests;
};

static inline string_view getString(const vector<vector<uint8_t>> &ST, uint32_t i) {
	return string_view(reinterpret_cast<const char*>(ST[i].data()), ST[i].size());
};

//////////////
//// NODE ////
//////////////

HashMap<vec2l> nodes;

static void readDense(const Proto::PrimitiveBlock &pb, const Proto::DenseNodes &dense, const vector<vector<uint8_t>> &ST, OSMData &data) {
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

		// Read tags
		NodeTags tags;
		while(*kv_it) {
			const string_view key = getString(ST, *(kv_it++));
			const string_view val = getString(ST, *(kv_it++));
			tags.readTag(key, val);
		}
		++ kv_it;

		// Process
		if(tags.place == Place::CITY && tags.capital >= 0 && tags.capital <= 6) {
			data.capitals.emplace_back(node, data.names.size());
			data.names.insert(data.names.end(), tags.name.begin(), tags.name.end());
			data.names.push_back('\0');
		}
	}
	if(kv_it != dense.keys_vals.end()) THROW_ERROR("Sizes mismatch in denseNodes...");
}

/////////////
//// WAY ////
/////////////

HashMap<vector<int64_t>> ways;

static void readWay(const Proto::Way &way, const vector<vector<uint8_t>> &ST, TmpData &data) {
	if(!way.lat.empty() || !way.lon.empty()) THROW_ERROR("lat and lon fields in Way are not supported");
	vector<int64_t> &w = ways[way.id] = way.refs;
	int64_t cur = 0;
	for(int64_t &ref : w) {
		cur += ref;
		ref = cur;
	}

	// Read Tags
	const int T = way.keys.size();
	if(T != (int) way.vals.size()) THROW_ERROR("Sizes mismatch in way's tags...");
	constexpr WayTags tags0;
	WayTags tags;
	for(int i = 0; i < T; ++i) {
		const string_view key = getString(ST, way.keys[i]);
		const string_view val = getString(ST, way.vals[i]);
		tags.readTag(key, val);
	}

	// Process
	const auto addRoad = [&](TmpRoad &roads) {
		for(const int64_t ref : w)
			roads.pts.push_back(nodes[ref]);
		roads.end();
	};
	if(tags.highway != tags0.highway) addRoad(data.roads[(uint32_t) tags.highway]);
	if(tags.waterway != tags0.waterway) addRoad(data.waterWays[(uint32_t) tags.waterway]);
	if(tags.boundary == Boundary::ADMINISTRATIVE && tags.admin_level >= 0 && tags.admin_level <= 4) {
		// TODO: different boundaries depending on admin level
		addRoad(data.boundaries);
	}
	if(tags.landuse == Landuse::FOREST || tags.natural == Natural::WOOD) {
		if(w.back() != w[0]) THROW_ERROR("Not closed");
		w.pop_back();
		if(w.size() < 3) THROW_ERROR("area with less than 3 nodes");
		addRoad(data.forests);
	}
}

//////////////////
//// RELATION ////
//////////////////

static void readRelation(const Proto::Relation &relation, const vector<vector<uint8_t>> &ST, OSMData &data) {
	const int T = relation.keys.size();
	if(T != (int) relation.vals.size()) THROW_ERROR("Sizes mismatch in relation's tags...");
	const int M = relation.memids.size();
	if(M != (int) relation.roles_sid.size()) THROW_ERROR("Sizes mismatch in relation's members...");
	if(M != (int) relation.types.size()) THROW_ERROR("Sizes mismatch in relation's members...");

	// Find type
	RelationTags tags;
	for(int i = 0; i < T; ++i) {
		const string_view key = getString(ST, relation.keys[i]);
		if(key != "type") continue;
		const string_view val = getString(ST, relation.vals[i]);
		tags.readType(val);
		break;
	}
	if((uint32_t) tags.type == RelationTags::UNDEF) return;
	tags.init();

	// Read other tags
	for(int i = 0; i < T; ++i) {
		const string_view key = getString(ST, relation.keys[i]);
		const string_view val = getString(ST, relation.vals[i]);
		tags.readTag(key, val);
	}

	// Process
	switch(tags.type) {
	using enum RelationType;
	case WATERWAY:
		// if(tags.waterway.waterway == Waterway::RIVER && tags.waterway.ref_sandre.size() > 1 && tags.waterway.ref_sandre[1] == '-') {
		// 	int64_t memid = 0;
		// 	for(int i = 0; i < M; ++i) {
		// 		memid += relation.memids[i];
		// 		if(relation.types[i] != Proto::Relation::MemberType::WAY) continue;
		// 		const string_view role = getString(ST, relation.roles_sid[i]);
		// 		if(role == "main_stream" && rivers.contains(memid))
		// 			mainRivers.push_back(memid);
		// 	}
		// }
		break;
	case RelationType::ROUTE: {
		const auto &route = tags.route;
		if(route.network == Network::FR_A_ROAD || route.network == Network::FR_N_ROAD) {
			int64_t id = 0;
			for(const int64_t mem : relation.memids) {
				id += mem;
				auto it = ways.find(id);
				if(it == ways.end()) continue;
				data.roadNames.emplace_back(nodes[it->second[0]], data.names.size());
				data.names.insert(data.names.end(), route.ref.begin(), route.ref.end());
				data.names.push_back('\0');
				break;
			}
		}
		break;
	}
	case RelationType::MULTIPOLYGON:
		if(tags.multipolygon.landuse == Landuse::FOREST) {
			int64_t memid = 0;
			for(int i = 0; i < M; ++i) {
				memid += relation.memids[i];
				if(relation.types[i] != Proto::Relation::MemberType::WAY) continue;
				const string_view role = getString(ST, relation.roles_sid[i]);
				if(role == "inner") {

				} else if(role == "outter") {

				}
			}
		}
		break;
	default:
		break;
	}
}

//////////////
//// MAIN ////
//////////////

int main(int argc, const char* argv[]) {
	if(argc != 3) {
		cerr << "Usage:\n";
		cerr << ">> " << argv[0] << " `in.osm.pbf` `out.osm.bin`\n";
		return 1;
	}

	BinStream input(argv[1]);
	uint32_t blobHeaderSize;
	vector<uint8_t> wire, blobData;
	bool hasHeader = false;

	OSMData data;
	TmpData tmpData;

	while(input.readInt(blobHeaderSize)) {
		// Get BlobHeader
		wire.resize(blobHeaderSize);
		input.read(reinterpret_cast<char*>(wire.data()), wire.size());
		const Proto::BlobHeader header(wire);

		// Get Blob
		wire.resize(header.datasize);
		input.read(reinterpret_cast<char*>(wire.data()), wire.size());
		const Proto::Blob blob(wire);
		blobData.resize(blob.raw_size);
		if(blob._data_choice == Proto::Blob::DATA_ZLIB_DATA) {
			uLongf data_size = blob.raw_size;
			if(uncompress(blobData.data(), &data_size, blob.data.zlib_data.data(), blob.data.zlib_data.size()) != Z_OK)
				THROW_ERROR("Failed to uncompress...");
		} else THROW_ERROR("Uncompression of blob data not implemented " + to_string(blob._data_choice));

		// Read Blob
		if(header.type == "OSMHeader") {
			if(hasHeader) THROW_ERROR("multiple OSMHeader...");
			hasHeader = true;
			readHeader(blobData, data);
		} else if(header.type == "OSMData") {
			if(!hasHeader) THROW_ERROR("OSMData blob before any OSMHeader...");
			const Proto::PrimitiveBlock pb(blobData);
			const auto &ST = pb.stringtable.s;
			for(const Proto::PrimitiveGroup &pg : pb.primitivegroup) {
				if(!pg.nodes.empty()) THROW_ERROR("Not implemented");
				if(!pg.changesets.empty()) THROW_ERROR("Not implemented");
				if(pg._has_dense) readDense(pb, pg.dense, ST, data);
				for(const Proto::Way &way : pg.ways) readWay(way, ST, tmpData);
				for(const Proto::Relation &relation : pg.relations) readRelation(relation, ST, data);
			}
		} else THROW_ERROR("Not recognized blob type: " + header.type);
	}

	input.close();

	// If no bbox, compute it
	if(data.bbox.min.x == numeric_limits<int64_t>::max())
		for(const vec2l &node : nodes | views::values)
			data.bbox.update(node);
	
	// Transfert tmpData ==> data
	const auto addTmpRoads = [&](TmpRoad &roads) {
		const uint32_t off = data.roads.size();
		data.roads.insert(data.roads.end(), roads.pts.begin(), roads.pts.end());
		roads.pts.clear();
		const auto r = roads.off | views::drop(1) | views::transform([&](const uint32_t o) { return off + o; });
		data.roadOffsets.insert(data.roadOffsets.end(), r.begin(), r.end());
		roads.off.clear();
	};
	data.roadOffsets.push_back(0);
	data.roadTypeOffsets[0] = 0;
	for(int i = 0; i < (int) RoadType::NUM; ++i) {
		addTmpRoads(tmpData.roads[i]);
		data.roadTypeOffsets[i+1] = data.roadOffsets.size()-1;
	}
	data.waterWayTypeOffsets[0] = data.roadOffsets.size()-1;
	for(int i = 0; i < (int) WaterWayType::NUM; ++i) {
		addTmpRoads(tmpData.waterWays[i]);
		data.waterWayTypeOffsets[i+1] = data.roadOffsets.size()-1;
	}
	data.boundaries.first = data.roadOffsets.size()-1;
	addTmpRoads(tmpData.boundaries);
	data.boundaries.second = data.roadOffsets.size()-1;
	data.forests.first = data.roadOffsets.size()-1;
	addTmpRoads(tmpData.forests);
	data.forests.second = data.roadOffsets.size()-1;

	// Write data
	data.write(argv[2]);
	
	return 0;
}