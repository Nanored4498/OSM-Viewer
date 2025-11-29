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

template<typename E, typename T=const char*>
constexpr auto make_array(initializer_list<pair<E, T>> init) {
    array<T, (size_t) E::NUM> arr{};
    for(const auto &[i, v] : init) arr[(size_t) i] = v;
    return arr;
}

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

enum class NodeKey {
	PLACE,
	NAME,
	CAPITAL,
	NUM
};
const StringSwitch nodeKeySwitch([]{ using enum NodeKey; return make_array<NodeKey>({
	{PLACE, "place"},
	{NAME, "name"},
	{CAPITAL, "capital"},
}); }());

enum class Place {
	CITY,
	NUM
};
const StringSwitch placeSwitch([]{ using enum Place; return make_array<Place>({
	{CITY, "city"},
}); }());

struct NodeTags {
	static inline constexpr uint32_t UNDEF = StringSwitch::NOT_FOUND;
	int capital = -1;
	Place place = (Place) UNDEF;
	string_view name;
};

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
			switch((NodeKey) nodeKeySwitch.feed(key)) {
			using enum NodeKey;
			case PLACE:
				tags.place = (Place) placeSwitch.feed(val);
				break;
			case NAME:
				tags.name = val;
				break;
			case CAPITAL:
				if(val == "yes") tags.capital = 2;
				else if(from_chars(val.begin(), val.end(), tags.capital).ec != errc())
					THROW_ERROR("capital is not a number: " + string(val));
				break;
			default:
				break;
			}
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

enum class WayKey {
	HIGHWAY,
	WATERWAY,
	BOUNDARY,
	ADMIN_LVL,
	LAND_USE,
	NATURAL,
	NUM
};
const StringSwitch wayKeySwitch([]{ using enum WayKey; return make_array<WayKey>({
	{HIGHWAY, "highway"},
	{WATERWAY, "waterway"},
	{BOUNDARY, "boundary"},
	{ADMIN_LVL, "admin_level"},
	{LAND_USE, "land_use"},
}); }());

const StringSwitch roadTypeSwitch([]{ using enum RoadType; return make_array<RoadType>({
	{MOTORWAY, "motorway"},
	{TRUNK, "trunk"},
	{PRIMARY, "primary"},
}); }());

const StringSwitch waterWaySwitch([] { using enum WaterWayType; return make_array<WaterWayType>({
	{RIVER, "river"},
}); }());

enum class Boundary {
	ADMIN,
	NUM
};
const StringSwitch boundarySwitch([]{ using enum Boundary; return make_array<Boundary>({
	{ADMIN, "administrative"}
}); }());

enum class LandUse {
	FOREST,
	NUM
};
const StringSwitch landUseSwitch([]{ using enum LandUse; return make_array<LandUse>({
	{FOREST, "forest"}
}); }());

enum class Natural {
	WOOD,
	NUM
};
const StringSwitch naturalSwitch([]{ using enum Natural; return make_array<Natural>({
	{WOOD, "wood"}
}); }());

struct WayTags {
	static inline constexpr uint32_t UNDEF = StringSwitch::NOT_FOUND;
	int admin_lvl = -1;
	RoadType highway = (RoadType) UNDEF;
	WaterWayType waterway = (WaterWayType) UNDEF;
	Boundary boundary = (Boundary) UNDEF;
	LandUse land_use = (LandUse) UNDEF;
	Natural natural = (Natural) UNDEF;
};

static void readWay(const Proto::Way &way, const vector<vector<uint8_t>> &ST, TmpData &data) {
	if(!way.lat.empty() || !way.lon.empty()) THROW_ERROR("lat and lon fields in Way are not supported");

	// Read Tags
	const int T = way.keys.size();
	if(T != (int) way.vals.size()) THROW_ERROR("Sizes mismatch in way's tags...");
	constexpr WayTags tags0;
	WayTags tags;
	for(int i = 0; i < T; ++i) {
		const string_view key = getString(ST, way.keys[i]);
		const string_view val = getString(ST, way.vals[i]);
		switch((WayKey) wayKeySwitch.feed(key)) {
		using enum WayKey;
		case HIGHWAY:
			tags.highway = (RoadType) roadTypeSwitch.feed(val);
			break;
		case WATERWAY:
			tags.waterway = (WaterWayType) waterWaySwitch.feed(val);
			break;
		case BOUNDARY:
			tags.boundary = (Boundary) boundarySwitch.feed(val);
			break;
		case ADMIN_LVL:
			if(from_chars(val.begin(), val.end(), tags.admin_lvl).ec != errc())
				THROW_ERROR("admin_level is not a number: " + string(val));
			break;
		case LAND_USE:
			tags.land_use = (LandUse) landUseSwitch.feed(val);
			break;
		case NATURAL:
			tags.land_use = (LandUse) landUseSwitch.feed(val);
			break;
		default:
			break;
		}
	}

	// Process
	const auto addRoad = [&](TmpRoad &roads) {
		int64_t last = 0;
		for(const int64_t ref : way.refs) {
			last += ref;
			roads.pts.push_back(nodes[last]);
		}
		roads.end();
	};
	if(tags.highway != tags0.highway) addRoad(data.roads[(uint32_t) tags.highway]);
	if(tags.waterway != tags0.waterway) addRoad(data.waterWays[(uint32_t) tags.waterway]);
	if(tags.boundary == Boundary::ADMIN && tags.admin_lvl >= 0 && tags.admin_lvl <= 4) {
		// TODO: different boundaries depending on admin level
		addRoad(data.boundaries);
	}
	if(tags.land_use == LandUse::FOREST || tags.natural == Natural::WOOD) {
		int64_t last = 0;
		for(const int64_t ref : way.refs) {
			last += ref;
			data.forests.pts.push_back(nodes[last]);
		}
		if(data.forests.pts.back() != data.forests.pts[data.forests.off.back()]) THROW_ERROR("Not closed");
		data.forests.pts.pop_back();
		data.forests.end();
	}
}

//////////////////
//// RELATION ////
//////////////////

enum class RelationType {
	WATERWAY,
	ROUTE,
	MULTIPOLYGON,
	NUM
};
const StringSwitch relationTypeSwitch([]{ using enum RelationType; return make_array<RelationType>({
	{WATERWAY, "waterway"},
	{ROUTE, "route"},
	{MULTIPOLYGON, "multipolygon"},
}); }());

enum class RelationKey {
	TYPE,
	WATERWAY,
	REF_SANDRE,
	NETWORK,
	REF,
	LANDUSE,
	NUM
};
const StringSwitch relationKeySwitch([]{ using enum RelationKey; return make_array<RelationKey>({
	{TYPE, "type"},
	{WATERWAY, "waterway"},
	{REF_SANDRE, "ref:sandre"},
	{NETWORK, "network"},
	{REF, "ref"},
	{LANDUSE, "landuse"},
}); }());

struct RelationTags {
	static inline constexpr uint32_t UNDEF = StringSwitch::NOT_FOUND;
	RelationType type = (RelationType) UNDEF;
	union{
		// waterway
		struct {
			WaterWayType waterway;
			string_view sandre;
		} waterway;
		// route
		struct {
			string_view network, ref;
		} route;
		// multipolygon
		struct {
			LandUse landuse;
		} multipolygon;
	};
	RelationTags() {};
	void init() {
		switch(type) {
		using enum RelationType;
		case WATERWAY:
			waterway.waterway = (WaterWayType) UNDEF;
			new(&waterway.sandre) string_view();
			break;
		case ROUTE:
			new(&route.network) string_view();
			new(&route.ref) string_view();
			break;
		case MULTIPOLYGON:
			multipolygon.landuse = (LandUse) UNDEF;
			break;
		default:
			break;
		}
	}
};

static void readRelation(const Proto::Relation &relation, const vector<vector<uint8_t>> &ST) {
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
		tags.type = (RelationType) relationTypeSwitch.feed(val);
		break;
	}
	if(tags.type == (RelationType) RelationTags::UNDEF) return;
	tags.init();

	// Read other tags
	for(int i = 0; i < T; ++i) {
		const string_view key = getString(ST, relation.keys[i]);
		const string_view val = getString(ST, relation.vals[i]);
		switch((RelationKey) relationKeySwitch.feed(key)) {
		using enum RelationKey;
		case WATERWAY:
			tags.waterway.waterway = (WaterWayType) waterWaySwitch.feed(val);
			break;
		case REF_SANDRE:
			tags.waterway.sandre = val;
			break;
		case NETWORK:
			tags.route.network = val;
			break;
		case REF:
			tags.route.ref = val;
			break;
		case LANDUSE:
			tags.multipolygon.landuse = (LandUse) landUseSwitch.feed(val);
			break;
		default:
			break;
		}
	}

	// Process
	switch(tags.type) {
	using enum RelationType;
	case WATERWAY:
		if(tags.waterway.waterway == WaterWayType::RIVER && tags.waterway.sandre.size() > 1 && tags.waterway.sandre[1] == '-') {
			int64_t memid = 0;
			for(int i = 0; i < M; ++i) {
				memid += relation.memids[i];
				if(relation.types[i] != Proto::Relation::MemberType::WAY) continue;
				// const string_view role = getString(ST, relation.roles_sid[i]);
				// if(role == "main_stream" && rivers.contains(memid))
				// 	mainRivers.push_back(memid);
			}
		}
		break;
	case RelationType::ROUTE:
		if(tags.route.network == "FR:A-road" || tags.route.network == "FR:N-road") {
			int64_t id = 0;
			for(const int64_t mem : relation.memids) {
				id += mem;
				// auto it = motorways.find(id);
				// if(it == motorways.end()) continue;
				// mainRoads.emplace_back(name, it->second[0]);
				break;
			}
		}
		break;
	case RelationType::MULTIPOLYGON:
		if(tags.multipolygon.landuse == LandUse::FOREST) {
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
				for(const Proto::Relation &relation : pg.relations) readRelation(relation, ST);
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
		data.roadTypeOffsets[i+1] = data.roadOffsets.size();
	}
	data.waterWayTypeOffsets[0] = data.roadOffsets.size();
	for(int i = 0; i < (int) WaterWayType::NUM; ++i) {
		addTmpRoads(tmpData.waterWays[i]);
		data.waterWayTypeOffsets[i+1] = data.roadOffsets.size();
	}
	data.boundaries.first = data.roadOffsets.size();
	addTmpRoads(tmpData.boundaries);
	data.boundaries.second = data.roadOffsets.size();
	data.forests.first = data.roadOffsets.size();
	addTmpRoads(tmpData.forests);
	data.forests.second = data.roadOffsets.size();
	
	// Write data
	data.write(argv[2]);
	
	return 0;
}