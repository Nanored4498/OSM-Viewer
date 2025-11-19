// Copyright (C) 2025, Coudert--Osmont Yoann
// SPDX-License-Identifier: AGPL-3.0-or-later
// See <https://www.gnu.org/licenses/>

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "triangulate.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <numeric>

using namespace std;

static bool turnLeft(const vector<vec2l> &pts, int i, int j, int k) {
	__int128_t ax = pts[j].x - pts[i].x, ay = pts[j].y - pts[i].y;
	__int128_t bx = pts[k].x - pts[j].x, by = pts[k].y - pts[j].y;
	return ax * by > ay * bx;
}

struct Edge {
	int b;
	Edge *prev, *next;
	void connect(Edge *e) {
		prev = e;
		e->next = this;
	}
	inline int a() const { return prev->b; };
};

vector<uint32_t> triangulate(const vector<vec2l> &pts) {
	if(pts.size() == 3)
		return {0u, 1u, 2u};
	vector<uint32_t> indices;

	// Using [V-E+F = 1-g] and [H = 3F = 2E-V] we get [H = 3V + 6(g-1)]
	// Here g = 0
	const int H = 3*pts.size() - 6;
	int newEdge = pts.size();
	unique_ptr<Edge[]> edges(new Edge[H]);
	vector<Edge*> in(pts.size()), out(pts.size());
	for(int i = 0; i < (int) pts.size(); ++i) {
		edges[i].b = i+1 == (int) pts.size() ? 0 : i+1;
		out[i] = &edges[i];
		if(i > 0) {
			in[i] = out[i-1];
			out[i]->connect(in[i]);
		}
	}
	in[0] = out.back();
	out[0]->connect(in[0]);

	const auto compY = [&](const int i, const int j)->bool {
		return pts[i].y < pts[j].y || (pts[i].y == pts[j].y && pts[i].x < pts[j].x);
	};
	vector<int> order(pts.size());
	ranges::iota(order, 0);
	ranges::sort(order, compY);
	if(!turnLeft(pts, in[order[0]]->a(), order[0], out[order[0]]->b)) { // the contour is clockwise
		// we make it counter-clockwise
		for(int i = 0; i < (int) pts.size(); ++i)
			out[i] = in[i]->prev;
		for(int i = 0; i < (int) pts.size(); ++i)
			swap(edges[i].prev, edges[i].next);
	}

	const auto compE = [&](const Edge *e1, const Edge *e2)->bool {
		int a1 = e1->prev ? e1->a() : e1->b, b1 = e1->b, a2 = e2->prev ? e2->a() : e2->b, b2 = e2->b;
		if(pts[a1].y > pts[b1].y) swap(a1, b1);
		if(pts[a2].y > pts[b2].y) swap(a2, b2);
		__int128_t xa1 = pts[a1].x, xa2 = pts[a2].x;
		int64_t ya1 = pts[a1].y, ya2 = pts[a2].y;
		if(ya1 < ya2) xa1 += __int128_t(ya2 - ya1) * (pts[b1].x - xa1) / (pts[b1].y - ya1);
		else if(ya1 > ya2) xa2 += __int128_t(ya1 - ya2) * (pts[b2].x - xa2) / (pts[b2].y - ya2);
		if(xa1 != xa2) return xa1 < xa2;
		else return e1 < e2;
	};
	const auto isMerge = [&](int j)->bool {
		const int i = in[j]->a(), k = out[j]->b;
		return compY(i, j) && compY(k, j) && !turnLeft(pts, i, j, k);
	};
	const auto addEdge = [&](Edge *e1, Edge *e2)->void {
		const int i = e1->b, j = e2->b;
		assert(newEdge+2 <= H);
		Edge* const new1 = &edges[newEdge++];
		Edge* const new2 = &edges[newEdge++];
		new1->b = j;
		new2->b = i;
		e1->next->connect(new2);
		e2->next->connect(new1);
		new1->connect(e1);
		new2->connect(e2);
		out.push_back(new1);
		out.push_back(new2);
		for(const Edge* const e : {e1, e2}) if(e->next->next->next == e) {
			indices.push_back(e->a());
			indices.push_back(e->b);
			indices.push_back(e->next->b);
		}
	};
	map<const Edge*, Edge*, decltype(compE)> BST(compE);
	const auto right_edge = [&](int j) {
		const Edge e { .b = j, .prev = nullptr };
		auto right = BST.upper_bound(&e);
		return right;
	};

	for(int j : order) {
		Edge *e_in = in[j], *e_out = out[j];
		int i = e_in->a(), k = e_out->b;
		if(compY(j, i)) {
			if(compY(j, k)) {
				if(turnLeft(pts, i, j, k)) { // start vertex
					BST[e_out] = e_in;
				} else { // split vertex
					auto right = right_edge(j);
					addEdge(e_in, right->second);
					right->second = e_in;
					BST[e_out] = e_out->prev;
				}
			} else { // left side vertex
				auto right = right_edge(j);
				assert(right != BST.end());
				if(isMerge(right->second->b)) addEdge(e_in, right->second);
				right->second = e_in;
			}
		} else {
			if(compY(k, j)) {
				if(turnLeft(pts, i, j, k)) { // end vertex
					Edge *helper = BST[e_in];
					if(isMerge(helper->b)) addEdge(e_in, helper);
					BST.erase(e_in);
				} else { // merge vertex
					Edge *helper = BST[e_in], *in2 = e_in;
					if(isMerge(helper->b)) {
						addEdge(e_in, helper);
						in2 = e_out->prev;
					}
					BST.erase(e_in);
					auto right = right_edge(j);
					if(isMerge(right->second->b)) addEdge(e_out->prev, right->second);
					right->second = in2;
				}
			} else { // right side vertex
				Edge *helper = BST[e_in];
				if(isMerge(helper->b)) addEdge(e_in, helper);
				BST.erase(e_in);
				BST[e_out] = e_out->prev;
			}
		}
	}
	
	int Es = out.size();
	for(int i = 0; i < Es; ++i) {
		if(out[i]->next == nullptr) continue;
		Edge *e = out[i], *ey0 = e, *ey1 = e, *e2 = e->next;
		while(e2 != e) {
			if(compY(e2->b, ey0->b)) ey0 = e2; 
			if(compY(ey1->b, e2->b)) ey1 = e2; 
			e2 = e2->next;
		}
		vector<Edge*> order = {ey0};
		e = ey0->prev;
		e2 = ey0->next;
		while(e != ey1 || e2 != ey1) {
			if(compY(e->b, e2->b)) {
				order.push_back(e);
				e = e->prev;
			} else {
				order.push_back(e2);
				e2 = e2->next;
			}
		}
		order.push_back(ey1);
		int n = order.size();

		vector<Edge*> stack = {order[0], order[1]};
		for(int i = 2; i+1 < n; ++i) {
			e = order[i];
			Edge *e2 = stack.back();
			if(compY(e->a(), e->b)) { // e is right side
				if(e->a() == e2->b) { // e2 is in the same side
					do {
						stack.pop_back();
						if(stack.empty()) break;
						Edge *e3 = stack.back();
						if(turnLeft(pts, e3->b, e2->b, e->b)) {
							addEdge(e, e3);
							e = e3->next;
							e2 = e3;
						} else break;
					} while(!stack.empty());
					stack.push_back(e->prev);
					stack.push_back(e);
				} else { // e2 is in the opposite side
					Edge *e3 = e->next;
					do {
						stack.pop_back();
						addEdge(e, e2);
						e2 = stack.back();
					} while(stack.size() > 1);
					stack.pop_back();
					e = e3->prev;
					stack.push_back(e->prev);
					stack.push_back(e);
				}
			} else { // Left side
				if(e->b == e2->a()) { // e2 is in the same side
					do {
						stack.pop_back();
						if(stack.empty()) break;
						Edge *e3 = stack.back();
						if(turnLeft(pts, e->b, e2->b, e3->b)) {
							addEdge(e, e3);
							e2 = e3;
						} else break;
					} while(!stack.empty());
					stack.push_back(e->next);
					stack.push_back(e);
				} else { // e2 is in the opposite side
					Edge *e3 = e;
					do {
						stack.pop_back();
						addEdge(e3, e2);
						e3 = e2->next;
						e2 = stack.back();
					} while(stack.size() > 1);
					stack.pop_back();
					stack.push_back(e->next);
					stack.push_back(e);
				}
			}
		}
		e = order.back();
		stack.pop_back();
		while(!stack.empty()) {
			Edge *e2 = stack.back();
			stack.pop_back();
			if(!stack.empty()) {
				addEdge(e, e2);
				if(compY(e2->a(), e2->b)) { // e2 is right side
					e = e2->next;
				}
			}
		}
		
		for(Edge *e : order) e->next = nullptr;
	}

	return indices;
}