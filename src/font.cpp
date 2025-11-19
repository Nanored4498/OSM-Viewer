// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#include "font.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

#include "utils.h"

using namespace std;

namespace Font {

struct TTFData {
	const uint8_t *loca, *head, *glyf, *hhea, *hmtx, *cmap;
	int numGlyphs;
	int16_t indexToLocFormat;
};

static uint16_t readUint16(const uint8_t *p) { return p[0]*256 + p[1]; }
static int16_t  readInt16(const uint8_t *p)  { return p[0]*256 + p[1]; }
static uint32_t readUint32(const uint8_t *p) { return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3]; }

enum PlatformID : uint16_t {
	UNICODE   = 0,
	MAC       = 1,
	MICROSOFT = 3
};

enum MicrosoftSpecificID : uint16_t {
	SYMBOL      = 0,
	UNICODE_BMP = 1,
	SHIFT_JIS   = 2,
	PRC         = 3,
	BIG_FIVE    = 4,
	JOHAB       = 6,
	UNICODE_UCS = 10
};

static uint32_t fintTableOffset(const uint8_t *data, const char *tag) {
	const int num_tables = readUint16(data+4);
	for(int i = 0; i < num_tables; ++i) {
		const int loc = 12 + 16*i;
		if(!memcmp(data+loc, tag, 4))
			return readUint32(data+loc+8);
	}
	return 0;
}

static TTFData findAllTables(const uint8_t *data) {
	TTFData info;

	const uint32_t cmap = fintTableOffset(data, "cmap");
	info.loca = data + fintTableOffset(data, "loca");
	info.head = data + fintTableOffset(data, "head");
	info.glyf = data + fintTableOffset(data, "glyf");
	info.hhea = data + fintTableOffset(data, "hhea");
	info.hmtx = data + fintTableOffset(data, "hmtx");

	if(!cmap || !info.loca || !info.head || !info.glyf || !info.hhea || !info.hmtx)
		THROW_ERROR("Missing tables in TTF file");

	const uint32_t t = fintTableOffset(data, "maxp");
	if(t) info.numGlyphs = readUint16(data+t+4);
	else info.numGlyphs = 0xffff;

	// Check for a Unicode subtable
	const uint32_t numberSubtables = readUint16(data + cmap + 2);
	info.cmap = 0;
	for(uint32_t i = 0; i < numberSubtables; ++i) {
		const uint32_t encoding_record = cmap + 4 + 8 * i;
		const uint16_t platformID = readUint16(data+encoding_record);
		const uint16_t platformSpecificID = readUint16(data+encoding_record+2);
		if(platformID == PlatformID::UNICODE) {
			if(platformSpecificID == 14) continue;
		} else if(platformID == PlatformID::MICROSOFT) {
			if(platformSpecificID != MicrosoftSpecificID::UNICODE_BMP
			&& platformSpecificID != MicrosoftSpecificID::UNICODE_UCS)
				continue;
		} else {
			continue;
		}
		info.cmap = data + cmap + readUint32(data+encoding_record+4);
		break;
	}
	if(!info.cmap)
		THROW_ERROR("No supported cmap encoding found (only unicode subtables are supported)");

	info.indexToLocFormat = readInt16(info.head + 50);
	return info;
}

static uint32_t charCode2GlyphID(const TTFData &info, uint32_t charCode) {
	const uint16_t format = readUint16(info.cmap + 0);
	switch(format) {
	case 0: // one byte encoding
		if(charCode > 0xffu) return 0;
		return *reinterpret_cast<const int8_t*>(info.cmap + 6 + charCode);

	case 4: { // two bytes encoding
		const uint16_t segCountX2 = readUint16(info.cmap+6);
		
		const uint8_t* endCode = info.cmap + 14;
		const uint8_t* const reservedPad = endCode + segCountX2;
		uint16_t searchRange = readUint16(info.cmap+8);
		endCode -= 2;
		while(searchRange >= 2) {
			const uint8_t* const search2 = endCode + searchRange;
			if(search2 < reservedPad && readUint16(search2) < charCode)
				endCode = search2;
			searchRange >>= 1;
		}
		endCode += 2;
		if(endCode == reservedPad) return 0;
	
		const uint8_t* const startCode = endCode + 2 + segCountX2;
		const uint16_t start = readUint16(startCode);
		if(charCode < start) return 0;

		const uint8_t* const idRangeOffset = startCode + 2*segCountX2;
		const uint16_t offset = readUint16(idRangeOffset);
		if(offset == 0)
			return (charCode + readInt16(startCode + segCountX2)) & 0xffffu;
		return readUint16(idRangeOffset + offset + 2*(charCode-start));
	}
	
	case 12: { // four bytes encoding
		const uint32_t nGroups = readUint32(info.cmap+12);
		const uint8_t* group = info.cmap + 16;
		uint32_t i = 0;
		for(uint32_t add = 1u<<31; add; add >>= 1) {
			const uint32_t i2 = i + add;	
			if(i2 < nGroups && charCode < readUint32(group + (ptrdiff_t) i2 * 12))
				i = i2;
		}
		if(++ i == nGroups) return 0;
		group += (ptrdiff_t) i * 12;
		const uint32_t endCharCode = readUint32(group+4);
		if(endCharCode < charCode) return 0;
		const uint32_t startCharCode = readUint32(group);
		const uint32_t startGlyphCode = readUint32(group+8);
		return startGlyphCode + charCode - startCharCode;
	}

	default:
		THROW_ERROR("cmap format " + to_string(format) + " is not supported");
	}
}

static const uint8_t* getGlyfOffset(const TTFData &info, int glyphID) {
	if(glyphID >= info.numGlyphs) return nullptr;
	uint32_t start, end;
	if(info.indexToLocFormat == 0) {
		start = (uint32_t) readUint16(info.loca + 2*glyphID) * 2;
		end = (uint32_t) readUint16(info.loca + 2*glyphID + 2) * 2;
	} else if(info.indexToLocFormat == 1) {
		start = readUint32(info.loca + 4*glyphID);
		end = readUint32(info.loca + 4*glyphID + 4);
	} else {
		return nullptr;
	}
	return start == end ? nullptr : info.glyf + start;
}

struct Box {
	int x0, y0;
	int x1, y1;
};

static Box getGlyphBox(const TTFData &info, int glyphID, float scale) {
	const uint8_t* g = getGlyfOffset(info, glyphID);
	if(g) {
		return Box {
			(int) floor( readInt16(g + 2) * scale),
			(int) floor(-readInt16(g + 8) * scale),
			(int) ceil ( readInt16(g + 6) * scale),
			(int) ceil (-readInt16(g + 4) * scale)
		};
	}
	return Box{};
}

static float getScale(const TTFData &info, float fontSize) {
	// We could also use unitsPerEm (in head table) for the scale
	// Instead we use the difference between the highest ascent and lowest descent
	const int16_t ascent = readInt16(info.hhea + 4);
	const int16_t descent = readInt16(info.hhea + 6);
	return fontSize / ((int) ascent - descent);
}

struct GlyphRect {
	int x, y;
	int w, h;
	bool missing;
};

static void getGlyphRects(const TTFData &info, const float fontSize, GlyphRect *rects) {
	const float scale = getScale(info, fontSize);
	bool has_missing_glyph = false;
	for(uint32_t i = 0; i < charCount; ++i) {
		const uint32_t glyphID = charCode2GlyphID(info, firstChar + i);
		if(!glyphID) {
			if(has_missing_glyph) {
				rects[i].missing = true;
				continue;
			}
			has_missing_glyph = true;
		}
		const auto [x0, y0, x1, y1] = getGlyphBox(info, glyphID, scale);
		rects[i].w = x1-x0;
		rects[i].h = y1-y0;
		rects[i].missing = false;
	}
}

static Atlas packRects(GlyphRect *rects) {
	constexpr int padding = 1;
	Atlas atlas;
	atlas.width = 256;
	int x = padding, y = padding, rowHeight = 0;
	uint32_t order[charCount];
	for(uint32_t i = 0; i < charCount; ++i) order[i] = i;
	sort(order, order+charCount, [&](uint32_t i, uint32_t j) {
		return rects[i].h < rects[j].h;
	});
	for(uint32_t i : order) {
		if(rects[i].missing) continue;
		if(x + rects[i].w + padding > atlas.width) {
			x = padding;
			y += rowHeight + padding;
			rowHeight = 0;
		}
		rects[i].x = x;
		rects[i].y = y;
		x += rects[i].w + padding;
		rowHeight = max(rowHeight, rects[i].h);
	}
	atlas.height = y + rowHeight + padding;
	atlas.img.reset(new uint8_t[atlas.width * atlas.height]());
	return atlas;
}

enum OutlineFlag : uint8_t {
	ON_CURVE   = 0x01u,
	X_SHORT    = 0x02u,
	Y_SHORT    = 0x04u,
	REPEAT     = 0x08u,
	X_POSITIVE = 0x10u,
	X_SAME     = 0x10u,
	Y_POSITIVE = 0x20u,
	Y_SAME     = 0x20u
};

enum ComponentFlag : uint16_t {
	ARG_1_AND_2_ARE_WORDS    = 0x01u,
	ARGS_ARE_XY_VALUES       = 0x02u,
	ROUND_XY_TO_GRID         = 0x04u,
	WE_HAVE_A_SCALE          = 0x08u,
	MORE_COMPONENTS          = 0x20u,
	WE_HAVE_AN_X_AND_Y_SCALE = 0x40u,
	WE_HAVE_A_TWO_BY_TWO     = 0x80u
};

enum VertexType {
	START,
	LINE,
	QUAD
};

struct Vertex {
	int16_t x, y, cx, cy;
	union {
		uint8_t flag, type;
	};

	void setStart(int16_t x, int16_t y){
		type = VertexType::START;
		this->x = x;
		this->y = y;
	}
	void setLine(int16_t x, int16_t y){
		type = VertexType::LINE;
		this->x = x;
		this->y = y;
	}
	void setQuad(int16_t x, int16_t y, int16_t cx, int16_t cy){
		type = VertexType::QUAD;
		this->x = x;
		this->y = y;
		this->cx = cx;
		this->cy = cy;
	}
};

template<typename T>
struct unique_array : unique_ptr<T[]> {
	unique_array() = default;
	unique_array(size_t size):
		unique_ptr<T[]>(new T[size]),
		_size(size)
		{}
	
	size_t size() const { return _size; }
	void resize(size_t size) {
		if(size > _size) THROW_ERROR("Can't increase size");
		_size = size;
	}

private:
	size_t _size = 0;
};

static unique_array<Vertex> getGlyphVertices(const TTFData &info, int glyphID) {
	const uint8_t* g = getGlyfOffset(info, glyphID);
	if(!g) return unique_array<Vertex>();

	const int numberOfContours = readInt16(g);
	int numberOfVertices = 0;

	// Simple glyph
	if(numberOfContours >= 0) {	
		const uint8_t *endPtsOfContours = g + 10;
		const uint16_t instructionLength = readUint16(endPtsOfContours + 2*numberOfContours);
		const uint8_t *data = endPtsOfContours + 2*numberOfContours + 2 + instructionLength;

		const int n = 1+readUint16(endPtsOfContours + 2*(numberOfContours-1));
		const int m = n + 2*numberOfContours;
		const int off = m - n;

		unique_array<Vertex> vertices(m);

		// Read flags
		uint8_t flagcount = 0;
		uint8_t flag = 0;
		for(int i = 0; i < n; ++i) {
			if(flagcount) {
				-- flagcount;
			} else {
				flag = *data++;
				if(flag & OutlineFlag::REPEAT)
					flagcount = *data++;
			}
			vertices[off+i].flag = flag;
		}

		// Read xCoordinates
		int16_t x = 0;
		for(int i = 0; i < n; ++i) {
			flag = vertices[off+i].flag;
			if(flag & OutlineFlag::X_SHORT) {
				const int16_t dx = *data++;
				x += (flag & OutlineFlag::X_POSITIVE) ? dx : -dx;
			} else if(!(flag & OutlineFlag::X_SAME)) {
				x += readInt16(data);
				data += 2;
			}
			vertices[off+i].x = x;
		}

		// Read yCoordinates
		int16_t y = 0;
		for(int i = 0; i < n; ++i) {
			flag = vertices[off+i].flag;
			if(flag & OutlineFlag::Y_SHORT) {
				const int16_t dy = *data++;
				y += (flag & OutlineFlag::Y_POSITIVE) ? dy : -dy;
			} else if(!(flag & OutlineFlag::Y_SAME)) {
				y += readInt16(data);
				data += 2;
			}
			vertices[off+i].y = y;
		}

		for(int i = 0, j = 0; i < numberOfContours; ++i) {
			// start curve
			const Vertex firstPt = vertices[off+j];
			const bool firstOnCurve = firstPt.flag & OutlineFlag::ON_CURVE;
			int x0, y0;
			if(firstOnCurve) {
				x0 = firstPt.x;
				y0 = firstPt.y;
			} else {
				if(vertices[off+j+1].flag & OutlineFlag::ON_CURVE) {
					x0 = vertices[off+j+1].x;
					y0 = vertices[off+j+1].y;
					++j;
				} else {
					x0 = ((int) firstPt.x + vertices[off+j+1].x) >> 1;
					y0 = ((int) firstPt.y + vertices[off+j+1].y) >> 1;
				}
			}
			vertices[numberOfVertices++].setStart(x0, y0);

			// loop over points
			const int endPt = readUint16(endPtsOfContours+i*2);
			bool lastOff = false;
			int16_t cx, cy;
			for(++j; j <= endPt; ++j) {
				x = vertices[off+j].x;
				y = vertices[off+j].y;
				if(vertices[off+j].flag & OutlineFlag::ON_CURVE) {
					if(lastOff)
						vertices[numberOfVertices++].setQuad(x, y, cx, cy);
					else
						vertices[numberOfVertices++].setLine(x, y);
					lastOff = false;
				} else {
					if(lastOff)
						vertices[numberOfVertices++].setQuad(((int)cx+x)>>1, ((int)cy+y)>>1, cx, cy);
					cx = x;
					cy = y;
					lastOff = true;
				}
			}

			// close curve
			if(firstOnCurve) {
				if(lastOff)
					vertices[numberOfVertices++].setQuad(x0, y0, cx, cy);
				else
					vertices[numberOfVertices++].setLine(x0, y0);
			} else {
				if(lastOff)
					vertices[numberOfVertices++].setQuad(((int)cx+firstPt.x)>>1, ((int)cy+firstPt.y)>>1, cx, cy);
				vertices[numberOfVertices++].setQuad(x0, y0, firstPt.x, firstPt.y);
			}
		}

		vertices.resize(numberOfVertices);
		return vertices;
	}

	// Compound glyph
	const uint8_t *data = g + 10;
	vector<unique_array<Vertex>> components;
	while(true) {
		const uint16_t flags = readInt16(data); data+=2;
		const uint16_t glyphIndex = readInt16(data); data+=2;

		int a = 1<<14, b = 0, c = 0, d = 1<<14, e = 0, f = 0;
		if(flags & ComponentFlag::ARGS_ARE_XY_VALUES) {
			if(flags & ComponentFlag::ARG_1_AND_2_ARE_WORDS) {
				e = readInt16(data); data+=2;
				f = readInt16(data); data+=2;
			} else {
				e = *reinterpret_cast<const int8_t*>(data); data+=1;
				f = *reinterpret_cast<const int8_t*>(data); data+=1;
			}
		} else THROW_ERROR("Matching point not implemented...");

		if(flags & ComponentFlag::WE_HAVE_A_SCALE) {
			a = d = readInt16(data); data+=2;
		} else if(flags & ComponentFlag::WE_HAVE_AN_X_AND_Y_SCALE) {
			a = readInt16(data); data+=2;
			d = readInt16(data); data+=2;
		} else if(flags & ComponentFlag::WE_HAVE_A_TWO_BY_TWO) {
			a = readInt16(data); data+=2;
			b = readInt16(data); data+=2;
			c = readInt16(data); data+=2;
			d = readInt16(data); data+=2;
		}

		int m = max(abs(a), abs(b));
		int n = max(abs(c), abs(d));
		if(abs(abs(a)-abs(c)) <= 8) m *= 2;
		if(abs(abs(b)-abs(d)) <= 8) n *= 2;
		e *= m;
		f *= n;
		// TODO: What does ROUND_XY_TO_GRID mean????

		const auto transform = [&](int16_t &x, int16_t &y) {
			const int16_t x0 = x;
			x = (a*x  + c*y + e) >> 14;
			y = (b*x0 + d*y + f) >> 14;
		};

		unique_array<Vertex> &component = components.emplace_back(getGlyphVertices(info, glyphIndex));
		const int compSize = component.size();
		for(int i = 0; i < compSize; ++i) {
			Vertex &v = component[i];
			transform(v.x, v.y);
			transform(v.cx, v.cy);
		}
		numberOfVertices += compSize;
		if(!(flags & ComponentFlag::MORE_COMPONENTS)) break;
	}

	unique_array<Vertex> vertices(numberOfVertices);
	Vertex* it = vertices.get();
	for(const unique_array<Vertex> &component : components)
		it = copy_n(component.get(), component.size(), it);
	return vertices;
}

struct Point {
	float x, y;
	Point(float x, float y): x(x), y(y) {}
};

struct CurveSet {
	unique_array<int> contourEnds;
	vector<Point> points;
	CurveSet(size_t size): contourEnds(size) {}
};

static void tesselateQuad(vector<Point> &points, float x0, float y0, float x1, float y1, float x2, float y2, float eps2, int depth) {
	if(depth > 16) return;
	const float mx = (x0 + 2*x1 + x2)/4;
	const float my = (y0 + 2*y1 + y2)/4;
	const float dx = (x0+x2)/2 - mx;
	const float dy = (y0+y2)/2 - my;
	if(dx*dx+dy*dy > eps2) {
		tesselateQuad(points, x0,y0, (x0+x1)/2.0f,(y0+y1)/2.0f, mx,my, eps2, depth+1);
		tesselateQuad(points, mx,my, (x1+x2)/2.0f,(y1+y2)/2.0f, x2,y2, eps2, depth+1);
	} else points.emplace_back(x2, y2);
	return;
}

static CurveSet linearizeCurves(unique_array<Vertex> &vertices, float eps) {
	int n = 0;
	for(int i = 0; i < (int) vertices.size(); ++i)
		if(vertices[i].flag == VertexType::START)
			++ n;
	CurveSet cs(n);
	if(!n) return cs;

	const float eps2 = eps * eps;
	cs.points.reserve(vertices.size());
	n = 0;
	float x = 0.f, y = 0.f;
	for(int i = 0; i < (int) vertices.size(); ++i) {
		switch(vertices[i].flag) {
		case VertexType::START:
			if(n) cs.contourEnds[n-1] = cs.points.size();
			++ n;
			[[fallthrough]];
		case VertexType::LINE:
			x = vertices[i].x, y = vertices[i].y;
			cs.points.emplace_back(x, y);
			break;
		case VertexType::QUAD:
			tesselateQuad(cs.points, x, y, vertices[i].cx, vertices[i].cy, vertices[i].x,  vertices[i].y, eps2, 0);
			x = vertices[i].x, y = vertices[i].y;
			break;
		}
	}
	cs.contourEnds[n-1] = cs.points.size();

	return cs;
}

struct Edge {
	float sx, sy, ey;
	float dx, dy;
	float sign;
};

static inline float insidePixelArea(int x, float x0, float y0, float x1, float y1) {
	return (y1 - y0) * (x+1 - (x0+x1)/2);
}

static void rasterizeEdge(const Edge &e, const float rowTop, float *row, float *rowSum, int width) {
	const float rowBot = rowTop+1;
	if(e.dx == 0) { // vertical edge
		if(e.sx >= width) return;
		const int x = e.sx;
		const float y0 = max(e.sy, rowTop);
		const float y1 = min(e.ey, rowBot);
		row[x] += e.sign * insidePixelArea(x, e.sx, y0, e.sx, y1);
		rowSum[x] += e.sign * (y1-y0);
		return;
	}

	// clip
	float ext, exb;
	float eyt, eyb;
	if(e.sy > rowTop) {
		ext = e.sx;
		eyt = e.sy;
	} else {
		ext = e.sx + e.dx * (rowTop - e.sy);
		eyt = rowTop;
	}
	if(e.ey < rowBot) {
		exb = e.sx + e.dx * (e.ey - e.sy);
		eyb = e.ey;
	} else {
		exb = e.sx + e.dx * (rowBot - e.sy);
		eyb = rowBot;
	}

	if((int) ext == (int) exb) { // one pixel
		const int x = ext;
		if(x < 0 || x >= width) return;
		row[x] += e.sign * insidePixelArea(x, ext, eyt, exb, eyb);
		rowSum[x] += e.sign * (eyb - eyt);
		return;
	}

	float dy = e.dy;
	if(ext > exb) { // make ext <= exb
		swap(ext, exb);
		dy = -dy;
	}
	if(exb < 0 || ext >= width) return;

	const int x1 = ext;
	const float step_rect = e.sign * dy;
	const float step_tri = step_rect / 2;
	int x = x1+1;
	float signed_area = step_rect * (x1+1 - ext);
	if(x1 >= 0) {
		row[x1] += signed_area * (x1+1 - ext) / 2;
	} else if(x < 0) {
		signed_area -= x*step_rect;
		x = 0;
	}
	const int x2 = min((int) exb, width);
	for(; x < x2; ++x) {
		row[x] += signed_area + step_tri;
		signed_area += step_rect;
	}

	if(x2 >= width) return;
	const float ycut = eyt + dy * (x2 - ext);
	row[x2] += signed_area + e.sign * insidePixelArea(x2, x2, ycut, exb, eyb);
	rowSum[x2] += e.sign * (eyb - eyt);
}

static void renderGlyph(const TTFData &info, uint8_t *img, const int imgWidth, const float scale, const int glyphID) {
	const Box box = getGlyphBox(info, glyphID, scale);
	const int glyphW = box.x1 - box.x0;
	const int glyphH = box.y1 - box.y0;
	if(!glyphW || !glyphH) return;

	constexpr float eps = .35f;
	unique_array<Vertex> vertices = getGlyphVertices(info, glyphID);
	CurveSet curveSet = linearizeCurves(vertices, eps / scale);
	if(!curveSet.contourEnds.size()) return;

	unique_ptr<Edge[]> edges(new Edge[curveSet.points.size()+1]);
	
	int n = 0;
	const vector<Point> &p = curveSet.points;
	for(int i = 0, k = 0; i < (int) curveSet.contourEnds.size(); ++i) {
		int j = curveSet.contourEnds[i]-1;
		for(; k < (int) curveSet.contourEnds[i]; j = k++) {
			if(p[j].y == p[k].y) continue;
			Edge &e = edges[n++];
			e.dx = (p[j].x - p[k].x) / (p[k].y - p[j].y);
			e.dy = e.dx != 0.f ? (1.f/e.dx) : 0.f;
			if(p[j].y < p[k].y) {
				e.sign = 1.f;
				e.sy = -p[k].y * scale;
				e.ey = -p[j].y * scale;
				e.sx = p[k].x * scale - box.x0;
			} else {
				e.sign = -1.f;
				e.sy = -p[j].y * scale;
				e.ey = -p[k].y * scale;
				e.sx = p[j].x * scale - box.x0;
			}
		}
	}

	sort(edges.get(), edges.get()+n, [&](const Edge &e1, const Edge &e2) {
		return e1.sy < e2.sy;
	});
	edges[n].sy = numeric_limits<float>::max();

	vector<int> active;
	unique_ptr<float[]> row(new float[2*glyphW+1]);
	float* rowSum = row.get() + glyphW;
	Edge* e = edges.get();
	for(int j = 0; j < glyphH; ++j) {
		const float rowTop = box.y0 + j;
		const float rowBot = rowTop + 1;

		for(int i = 0; i < (int) active.size();)
			if(edges[active[i]].ey <= rowTop) {
				active[i] = active.back();
				active.pop_back();
			} else ++i;

		for(; e->sy <= rowBot; ++e) {
			active.push_back(e - edges.get());
		}

		fill_n(row.get(), 2*glyphW+1, 0.f);
		for(int i : active)
			rasterizeEdge(edges[i], rowTop, row.get(), rowSum+1, glyphW);

		float sum = 0;
		for(int i = 0; i < glyphW; ++i) {
			sum += rowSum[i];
			img[j*imgWidth + i] = clamp(int((row[i] + sum)*256.f), 0, 255);
		}
	}
}

static void renderGlyphs(Atlas &atlas, const TTFData &info, const float fontSize, GlyphRect *rects) {
	int missing_glyph = -1;
	const float scale = getScale(info, fontSize);
	const uint16_t numOfLongHorMetrics = readUint16(info.hhea + 34);
	for(uint32_t j = 0; j < charCount; ++j) {
		GlyphRect &r = rects[j];
		if(r.missing) {
			if(missing_glyph == -1)
				THROW_ERROR("Missing glyph encountered too early");
			atlas.charPositions[j] = atlas.charPositions[missing_glyph];
			continue;
		}
	
		const int glyphID = charCode2GlyphID(info, firstChar + j);
		renderGlyph(info,
			atlas.img.get() + r.x + r.y*atlas.width,
			atlas.width,
			scale,
			glyphID);

		const uint16_t advanceWidth = readInt16(info.hmtx + 4*min(glyphID, numOfLongHorMetrics-1));
		const auto [x0,y0,x1,y1] = getGlyphBox(info, glyphID, scale);
		CharPosition &cp = atlas.charPositions[j];
		cp.x0       = r.x;
		cp.y0       = r.y;
		cp.x1       = r.x + r.w;
		cp.y1       = r.y + r.h;
		cp.xadvance = scale * advanceWidth;
		cp.xoff     = x0;
		cp.yoff     = y0;

		if(!glyphID) missing_glyph = j;
	}
}

Atlas getTTFAtlas(const char* fileName, const float fontSize) {
	ifstream f(fileName);
	f.seekg(0, ios::end);
	const size_t size = f.tellg();
	f.seekg(0, ios::beg);
	unique_ptr<uint8_t[]> fontData(new uint8_t[size]);
	f.read(reinterpret_cast<char*>(fontData.get()), size);

	// Check tag (Only support OpenType 1.0)
	if(memcmp(reinterpret_cast<char*>(fontData.get()), "\0\1\0\0", 4))
		THROW_ERROR("bad TTF tag");

	GlyphRect rects[charCount];

	TTFData info = findAllTables(fontData.get());
	getGlyphRects(info, fontSize, rects);
	Atlas atlas = packRects(rects);
	renderGlyphs(atlas, info, fontSize, rects);
	return atlas;
}

}