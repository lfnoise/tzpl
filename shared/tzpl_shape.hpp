// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  tzpl_shape.hpp
//
//  C++ helpers over tzpl_Shape (tzpl_plugin_abi.h): row-major tensors of up
//  to TZPL_MAX_RANK dimensions. Leading unused dims are 1. Adapted from
//  tensor-ugens shape.hpp / graph.cpp (broadcastChannel).
//
//  Connecting a source of shape S to a destination of shape D maps channels
//  per dimension:
//    D[d] >= S[d]  broadcast: destination index w reads source index w % S[d]
//    D[d] <  S[d]  fold: destination index w sums every source index j with
//                  j % D[d] == w
//  A connection with no folding dimension is pure pointer remapping.
//

#ifndef tzpl_shape_hpp
#define tzpl_shape_hpp

#include "tzpl_plugin_abi.h"
#include <cstdint>
#include <numeric>
#include <string>

inline bool operator==(tzpl_Shape const& a, tzpl_Shape const& b) {
    for (int d = 0; d < TZPL_MAX_RANK; ++d)
        if (a.dims[d] != b.dims[d]) return false;
    return true;
}

namespace tzpl {

constexpr int kMaxRank = TZPL_MAX_RANK;

inline tzpl_Shape shapeScalar() { return tzpl_Shape{{1, 1, 1, 1}}; }
inline tzpl_Shape shapeRow(int n) { return tzpl_Shape{{1, 1, 1, (uint32_t)n}}; }
inline tzpl_Shape shapeMatrix(int rows, int cols) {
    return tzpl_Shape{{1, 1, (uint32_t)rows, (uint32_t)cols}};
}
inline tzpl_Shape shapeCuboid(int planes, int rows, int cols) {
    return tzpl_Shape{{1, (uint32_t)planes, (uint32_t)rows, (uint32_t)cols}};
}

inline int chans(tzpl_Shape const& s) {
    return (int)(s.dims[0] * s.dims[1] * s.dims[2] * s.dims[3]);
}

// Number of dimensions after dropping leading 1s (a scalar has rank 0).
inline int rank(tzpl_Shape const& s) {
    for (int d = 0; d < kMaxRank; ++d)
        if (s.dims[d] != 1) return kMaxRank - d;
    return 0;
}

// Row-major strides: the last dimension has stride 1. Returns the size.
inline int strides(tzpl_Shape const& s, int out[kMaxRank]) {
    int size = 1;
    for (int d = kMaxRank - 1; d >= 0; --d) {
        out[d] = size;
        size *= (int)s.dims[d];
    }
    return size;
}

inline bool validShape(tzpl_Shape const& s) {
    for (int d = 0; d < kMaxRank; ++d)
        if (s.dims[d] < 1) return false;
    return true;
}

// Each dimension is the least common multiple of a's and b's.
inline tzpl_Shape broadcast(tzpl_Shape const& a, tzpl_Shape const& b) {
    tzpl_Shape out;
    for (int d = 0; d < kMaxRank; ++d)
        out.dims[d] = std::lcm(a.dims[d], b.dims[d]);
    return out;
}

// Unflatten channel w of shape s into per-dimension indices.
inline void index(tzpl_Shape const& s, int w, int idx[kMaxRank]) {
    for (int d = kMaxRank - 1; d >= 0; --d) {
        idx[d] = w % (int)s.dims[d];
        w /= (int)s.dims[d];
    }
}

inline int flatten(tzpl_Shape const& s, int const idx[kMaxRank]) {
    int w = 0;
    for (int d = 0; d < kMaxRank; ++d) w = w * (int)s.dims[d] + idx[d];
    return w;
}

// The source channel that output channel w reads, broadcasting cyclically in
// every dimension (tensor-ugens broadcastChannel).
inline int broadcastChannel(tzpl_Shape const& src, tzpl_Shape const& out, int w) {
    int srcStrides[kMaxRank];
    strides(src, srcStrides);
    int rem = w, p = 0;
    for (int d = kMaxRank - 1; d >= 0; --d) {
        p += (rem % (int)out.dims[d] % (int)src.dims[d]) * srcStrides[d];
        rem /= (int)out.dims[d];
    }
    return p;
}

// True when a source of shape src reaches a destination of shape dst by
// pointer remapping alone (no dimension folds).
inline bool broadcastsTo(tzpl_Shape const& src, tzpl_Shape const& dst) {
    for (int d = 0; d < kMaxRank; ++d)
        if (dst.dims[d] < src.dims[d]) return false;
    return true;
}

// The destination channel a source channel j lands on under the
// broadcast/fold rule, for a dst no larger than src in any dimension that
// folds. Used to build fold tables: dst channel w sums every j with
// foldChannel(src, dst, j) == w, for j over the source. Broadcast dimensions
// (dst >= src) are expanded by the caller, which repeats j per copy.
inline int foldChannel(tzpl_Shape const& src, tzpl_Shape const& dst, int j) {
    int idx[kMaxRank];
    index(src, j, idx);
    for (int d = 0; d < kMaxRank; ++d) idx[d] %= (int)dst.dims[d];
    return flatten(dst, idx);
}

// "6", "2x3", "4x2x3": the dims after the leading 1s, joined by `sep`.
inline std::string shapeString(tzpl_Shape const& s, char const* sep = "x") {
    int r = rank(s);
    if (r == 0) return "1";
    std::string out;
    for (int d = kMaxRank - r; d < kMaxRank; ++d) {
        if (!out.empty()) out += sep;
        out += std::to_string(s.dims[d]);
    }
    return out;
}

} // namespace tzpl

#endif // tzpl_shape_hpp
