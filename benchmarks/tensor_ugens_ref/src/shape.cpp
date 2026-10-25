//
//  shape.cpp
//  tensor-ugens
//

#include "shape.hpp"
#include <algorithm>
#include <cstdlib>

int Shape_size(Shape* s) {
    int size = 1;
    for (int i = 0; i < kMaxRank; ++i) {
        size *= s->dims[i];
    }
    return size;
}

int Shape_strides(Shape* s, int strides[4]) {
    int size = 1;
    // Row-major: the last dimension varies fastest (stride 1).
    for (int i = kMaxRank - 1; i >= 0; --i) {
        strides[i] = size;
        size *= s->dims[i];
    }
    return size;
}

bool Shape_equal(const Shape* a, const Shape* b) {
    return std::equal(a->dims, a->dims + kMaxRank, b->dims);
}

Shape Shape_scalar() {
    return Shape{{1, 1, 1, 1}};
}

Shape Shape_row(int n) {
    return Shape{{1, 1, 1, n}};
}

Shape Shape_col(int n) {
    return Shape{{1, 1, n, 1}};
}

Shape Shape_plane(int n) {
    return Shape{{1, n, 1, 1}};
}

Shape Shape_matrix(int rows, int cols) {
    return Shape{{1, 1, rows, cols}};
}

Shape Shape_cuboid(int planes, int rows, int cols) {
    return Shape{{1, planes, rows, cols}};
}

static int gcd(int u, int v)
{
	int t;
	u = std::abs(u);
	v = std::abs(v);
	if (u <= 1 || v <= 1) return 1;
	while (u>0) {
		if (u<v) { t=u; u=v; v=t; }
		u = u % v;
	}
	return v;
}

static int lcm(int u, int v)
{
	return u/gcd(u,v) * v;
}

Shape Shape_broadcast(Shape* a, Shape* b) {
    Shape out;
    for (int i = 0; i < kMaxRank; ++i) {
        out.dims[i] = lcm(a->dims[i], b->dims[i]);
    }
    return out;
}
