// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include <bitset>
#include <cassert>
#include <concepts>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
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

template<typename T, typename E>
requires is_enum_v<E>
struct TmpContainer {
protected:
	using BS = bitset<static_cast<size_t>(E::COUNT)>;
public:
	vector<T> data;
	vector<uint32_t> off = {0};
	struct Flag : BS {
		void set(E x) { BS::set((size_t) x); }
		bool test(E x) { return BS::test((size_t) x); }
	} flags;
	void end() {
		off.push_back(data.size());
	}
};

enum class TmpRoadFlag {
	RENDERED_AREA,
	COUNT
};
using TmpRoad = TmpContainer<vec2l, TmpRoadFlag>;

struct TmpRef {
	TmpRoad* storage = nullptr;
	uint32_t ind = 0;
};
enum class TmpRelFlag {
	COUNT
};
using TmpRelation = TmpContainer<TmpRef, TmpRoadFlag>;

struct TmpData {
	array<TmpRoad, (size_t) RoadType::NUM> roads;
	array<TmpRoad, (size_t) WaterWayType::NUM> waterWays;
	TmpRoad boundaries, forests, misc;
	TmpRelation forestsR;
	TmpData() {
		forests.flags.set(TmpRoadFlag::RENDERED_AREA);
	}
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

struct Way {
	vector<int64_t> way;
	TmpRef ref{};
	bool isClosed() const { return way.size() >= 2 && way[0] == way.back(); };
};

static void addRoad(TmpRoad &roads, Way &w) {
	w.ref.storage = &roads;
	w.ref.ind = roads.off.size()-1;
	if(roads.flags.test(TmpRoadFlag::RENDERED_AREA) && !w.isClosed())
		THROW_ERROR("rendered area should be closed");
	roads.data.insert_range(roads.data.end(),
		w.way
		| views::drop(roads.flags.test(TmpRoadFlag::RENDERED_AREA) ? 1 : 0)
		| views::transform([&](const int64_t id) {
			return nodes[id];
		})
	);
	roads.end();
};

HashMap<Way> ways;

static void readWay(const Proto::Way &way, const vector<vector<uint8_t>> &ST, TmpData &data) {
	if(!way.lat.empty() || !way.lon.empty()) THROW_ERROR("lat and lon fields in Way are not supported");
	Way &w = ways[way.id] = {way.refs};
	int64_t cur = 0;
	for(int64_t &ref : w.way) {
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
	if((uint32_t) tags.highway != tags.UNDEF) addRoad(data.roads[(uint32_t) tags.highway], w);
	else if((uint32_t) tags.waterway != tags.UNDEF) addRoad(data.waterWays[(uint32_t) tags.waterway], w);
	else if(tags.boundary == Boundary::ADMINISTRATIVE && tags.admin_level >= 0 && tags.admin_level <= 4) {
		// TODO: different boundaries depending on admin level
		addRoad(data.boundaries, w);
	} else if(tags.landuse == Landuse::FOREST || tags.natural == Natural::WOOD) {
		if(w.way.back() != w.way[0]) THROW_ERROR("Not closed");
		if(w.way.size() < 4) THROW_ERROR("area with less than 3 nodes");
		addRoad(data.forests, w);
	}
}

//////////////////
//// RELATION ////
//////////////////

static void processMultipolygon(const Proto::Relation &relation, const vector<vector<uint8_t>> &ST, TmpRoad &misc, TmpRelation &polygons) {
	struct Component {
		vector<Way*> outer, inner;
		__int128_t area = 0;
	};
	vector<Component> cs;
	vector<vector<Way*>> inners;
	vector<Way*> outerWays, innerWays;
	int64_t memid = 0;
	const int M = relation.memids.size();
	for(int i = 0; i < M; ++i) {
		memid += relation.memids[i];
		if(relation.types[i] != Proto::Relation::MemberType::WAY) {
			// Currently ignore members that are not ways
			continue;
		}
		const string_view role = getString(ST, relation.roles_sid[i]);
		const auto it = ways.find(memid);
		if(it == ways.end()) THROW_ERROR("way not found");
		Way &w = it->second;
		if(role == "outer") {
			if(w.ref.storage && w.ref.storage->flags.test(TmpRoadFlag::RENDERED_AREA)) {
				// Currently ignore outer members that are already rendered
				continue;
			}
			if(w.isClosed()) cs.emplace_back().outer.push_back(&w);
			else outerWays.push_back(&w);
		} else if(role == "inner") {
			if(w.isClosed()) inners.emplace_back().push_back(&w);
			else innerWays.push_back(&w);
		} else {
			// Currently ignore members that are neither outer nor inner
			continue;
		}
	}

	// Merge ways
	// Slow implementation, could be improved
	const auto merge = [&](const vector<Way*> &ways, vector<vector<Way*>> &groups)->bool {
		const uint32_t N = ways.size();
		if(!N) return true;
		unique_ptr<bool[]> seen(new bool[N]());
		unique_ptr<uint32_t[]> G(new uint32_t[2*N]);
		constexpr uint32_t END = 1u<<31;
		iota(G.get(), G.get()+N, 0);
		iota(G.get()+N, G.get()+2*N, END);
		const auto getNode = [&](uint32_t i)->int64_t {
			if(i&END) return ways[i^END]->way.back();
			return ways[i]->way[0];
		};
		span<uint32_t> GSpan(G.get(), 2*N);
		ranges::sort(GSpan, less<int64_t>{}, getNode);
		for(uint32_t i = 0; i < N; ++i) {
			if(seen[i]) continue;
			seen[i] = true;
			vector<Way*> &g = groups.emplace_back(1, ways[i]);
			const int64_t first = getNode(i);
			int64_t last = getNode(END|i);
			while(last != first) {
				auto it = ranges::lower_bound(GSpan, last, less<int64_t>{}, getNode);
				if(it+1 >= GSpan.end() || getNode(it[1]) != last)
					THROW_ERROR("Node in only one way");
				if(it+2 < GSpan.end() && getNode(it[2]) == last) {
					// Currently ignore ill-formed multipolygons
					return false;
				}
				if(ways[(*it)&(~END)] == g.back()) ++it;
				const uint32_t j = (*it)&(~END);
				assert(!seen[j]);
				seen[j] = true;
				g.push_back(ways[j]);
				last = getNode((*it)^END);
			}
		}
		return true;
	};
	vector<vector<Way*>> outers;
	if(!merge(outerWays, outers)) return;
	if(!merge(innerWays, inners)) return;
	cs.resize(cs.size() + outers.size());
	for(uint32_t i = 0; i < outers.size(); ++i)
		cs[cs.size()-1-i].outer = std::move(outers[i]);
	
	// Sort outers by increasing area
	for(Component &c : cs) {
		int64_t last = c.outer[0]->way[0];
		for(const Way* way : c.outer) {
			const vector<int64_t> &w = way->way;
			__int128_t wa = 0;
			for(uint32_t i = 1; i < w.size(); ++i) {
				const vec2l &a = nodes[w[i-1]];
				const vec2l &b = nodes[w[i]];
				wa += __int128_t(a.x - b.x) * (a.y + b.y);
			}
			if(last == w[0]) {
				c.area += wa;
				last = w.back();
			} else {
				c.area -= wa;
				last = w[0];
			}
		}
		c.area = abs(c.area);
	}
	ranges::sort(cs, less<__int128_t>{}, [&](const Component &c) { return c.area; });

	// Add inners to components
	for(const vector<Way*> &in : inners) {
		for(Component &c : cs) {
			for(const Way* inWay : in) {
				for(const int64_t id : inWay->way) {
					const vec2l &v = nodes[id];
					uint32_t winding = 0;
					for(const Way* way : c.outer) {
						const vector<int64_t> &w = way->way;
						for(uint32_t i = 1; i < w.size(); ++i) {
							vec2l a = nodes[w[i-1]];
							vec2l b = nodes[w[i]];
							if(a.y > b.y) swap(a, b);
							if(v.y < a.y) continue;
							if(b.y <= v.y) continue;
							const __int128_t diff = __int128_t(v.x-a.x) * (b.y-a.y) - __int128_t(b.x-a.x) * (v.y-a.y);
							if(diff > 0) ++ winding;
							else if(diff == 0) goto v_over_outer;
						}
					}
					if(winding&1) {
						c.inner.insert_range(c.inner.end(), in);
						goto outer_found;
					}
					goto not_inside;
					v_over_outer:
					continue;
				}
			}
			THROW_ERROR("All nodes of inner loop over an outer loop");
			not_inside:
			continue;
		}
		// If reached here, probably an outer loop have been remove because it was already a rendered area
		// we juste skip the inner loop
		outer_found:
		continue;
	}

	// Add components to polygons
	for(const Component &c : cs) {
		for(const auto ways : {&c.outer, &c.inner}) {
			for(Way *w : *ways) {
				if(!w->ref.storage) addRoad(misc, *w);
				polygons.data.push_back(w->ref);
			}
		}
		if(polygons.off.size() == 125) cerr << relation.id << endl;
		polygons.end();
	}
}

static void readRelation(const Proto::Relation &relation, const vector<vector<uint8_t>> &ST, OSMData &data, TmpData &tmpData) {
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
				const auto it = ways.find(id);
				if(it == ways.end()) continue;
				data.roadNames.emplace_back(nodes[it->second.way[0]], data.names.size());
				data.names.insert(data.names.end(), route.ref.begin(), route.ref.end());
				data.names.push_back('\0');
				break;
			}
		}
		break;
	}
	case RelationType::MULTIPOLYGON:
		if(tags.multipolygon.landuse == Landuse::FOREST)
			processMultipolygon(relation, ST, tmpData.misc, tmpData.forestsR);
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
				for(const Proto::Relation &relation : pg.relations) readRelation(relation, ST, data, tmpData);
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
		data.roads.insert_range(data.roads.end(), roads.data);
		roads.data.clear();
		roads.data.shrink_to_fit();
		roads.off[0] = data.roadOffsets.size()-1;
		data.roadOffsets.insert_range(data.roadOffsets.end(),
			roads.off
			| views::drop(1)
			| views::transform([&](const uint32_t o) { return off + o; })
		);
		roads.off.resize(1);
		roads.off.shrink_to_fit();
	};
	const auto addArrayRoads = [&](auto &off, auto &tmp) {
		off[0] = data.roadOffsets.size()-1;
		for(uint32_t i = 0; i < tmp.size(); ++i) {
			addTmpRoads(tmp[i]);
			off[i+1] = data.roadOffsets.size()-1;
		}
	};
	const auto addPairRoads = [&](pair<uint32_t, uint32_t> &off, TmpRoad &tmp) {
		off.first = data.roadOffsets.size()-1;
		addTmpRoads(tmp);
		off.second = data.roadOffsets.size()-1;
	};
	data.roadOffsets.push_back(0);
	addArrayRoads(data.roadTypeOffsets, tmpData.roads);
	addArrayRoads(data.waterWayTypeOffsets, tmpData.waterWays);
	addPairRoads(data.boundaries, tmpData.boundaries);
	addPairRoads(data.forests, tmpData.forests);
	addTmpRoads(tmpData.misc);

	const auto addTmpRel = [&](TmpRelation &rel) {
		const uint32_t off = data.refs.size();
		data.refs.insert_range(data.refs.end(), rel.data | views::transform([&](const TmpRef &ref)->uint32_t {
			assert(ref.storage);
			return ref.storage->off[0] + ref.ind;
		}));
		rel.data.clear();
		rel.data.shrink_to_fit();
		data.refOffsets.insert_range(data.refOffsets.end(),
			rel.off
			| views::drop(1)
			| views::transform([&](const uint32_t o) { return off + o; })
		);
		rel.off.clear();
		rel.off.shrink_to_fit();
	};
	data.refOffsets.push_back(0);
	data.forestsR.first = data.refOffsets.size()-1;
	addTmpRel(tmpData.forestsR);
	data.forestsR.second = data.refOffsets.size()-1;

	// Write data
	data.write(argv[2]);
	
	return 0;
}