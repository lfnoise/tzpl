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
//  tzpl_delay_interp.hpp
//  Delay line interpolation functions for generated plugin code.
//
//  Two layers:
//    tzpl_interp_*  -- pure arithmetic kernels, work for scalar and SIMD types
//    tzpl_delay_*   -- scalar gather + kernel, for non-SIMD codegen paths
//
//  The SIMD codegen generates the gather inline (per-lane buffer reads)
//  and calls the tzpl_interp_* kernels on the resulting SIMD vectors.
//

#ifndef tzpl_delay_interp_hpp
#define tzpl_delay_interp_hpp

#include <cmath>
#include <cstdint>

// ===========================================================================
// Interpolation kernels -- pure arithmetic, no buffer access.
// T may be a scalar (f64) or a SIMD vector (f64x4).
// Scalar-vector arithmetic (e.g. T * 0.5) is supported by Clang vector types.
// ===========================================================================

template<typename T>
inline T tzpl_interp_linear(T s0, T s1, T frac) {
    return s0 + frac * (s1 - s0);
}

template<typename T>
inline T tzpl_interp_cubic(T ym1, T y0, T y1, T y2, T frac) {
    T c0 = y0;
    T c1 = (y1 - ym1) * 0.5;
    T c2 = ym1 - y0 * 2.5 + y1 * 2.0 - y2 * 0.5;
    T c3 = (y2 - ym1) * 0.5 + (y0 - y1) * 1.5;
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

template<typename T>
inline T tzpl_interp_lagrange(T s0, T s1, T s2, T s3,
                               T s4, T s5, T s6, T s7, T frac) {
    T f0 = frac + 3.0, f1 = frac + 2.0, f2 = frac + 1.0, f3 = frac;
    T f4 = frac - 1.0, f5 = frac - 2.0, f6 = frac - 3.0, f7 = frac - 4.0;

    // Prefix products: L[k] = f[0] * ... * f[k-1]
    T L1 = f0;
    T L2 = L1 * f1;
    T L3 = L2 * f2;
    T L4 = L3 * f3;
    T L5 = L4 * f4;
    T L6 = L5 * f5;
    T L7 = L6 * f6;

    // Suffix products: R[k] = f[k+1] * ... * f[7]
    T R6 = f7;
    T R5 = R6 * f6;
    T R4 = R5 * f5;
    T R3 = R4 * f4;
    T R2 = R3 * f3;
    T R1 = R2 * f2;
    T R0 = R1 * f1;

    // L[0]=1, R[7]=1 -- those terms simplify
    return (R0 * (1.0 / -5040.0)) * s0
         + (L1 * R1 * (1.0 / 720.0)) * s1
         + (L2 * R2 * (1.0 / -240.0)) * s2
         + (L3 * R3 * (1.0 / 144.0)) * s3
         + (L4 * R4 * (1.0 / -144.0)) * s4
         + (L5 * R5 * (1.0 / 240.0)) * s5
         + (L6 * R6 * (1.0 / -720.0)) * s6
         + (L7 * (1.0 / 5040.0)) * s7;
}

// Sinc kernel: weighted sum with pre-computed coefficients.
template<typename T>
inline T tzpl_interp_sinc(T s0, T s1, T s2, T s3,
                           T s4, T s5, T s6, T s7,
                           T c0, T c1, T c2, T c3,
                           T c4, T c5, T c6, T c7) {
    return c0 * s0 + c1 * s1 + c2 * s2 + c3 * s3
         + c4 * s4 + c5 * s5 + c6 * s6 + c7 * s7;
}

// ===========================================================================
// Sinc coefficient table -- Hanning windowed sinc, 8 points.
// Initialized at load time (dlopen), not lazily, so no heavy computation
// happens on the RT thread.
// ===========================================================================

struct TzplSincTable {
    static constexpr int SIZE = 2048;
    static constexpr int POINTS = 8;
    double coeffs[SIZE + 1][POINTS];

    TzplSincTable() {
        for (int i = 0; i <= SIZE; i++) {
            double frac = double(i) / double(SIZE);
            for (int j = 0; j < POINTS; j++) {
                double point_pos = double(j - 3);
                double x = frac - point_pos;
                coeffs[i][j] = hanning_sinc(x);
            }
        }
    }

    static double hanning_sinc(double x) {
        constexpr double pi = 3.14159265358979323846;
        constexpr double half_width = 4.0;
        if (std::abs(x) < 1e-10) return 1.0;
        if (std::abs(x) >= half_width) return 0.0;
        double sinc_val = std::sin(pi * x) / (pi * x);
        double hann_val = 0.5 * (1.0 + std::cos(pi * x / half_width));
        return sinc_val * hann_val;
    }
};

inline const TzplSincTable tzpl_sinc_table;

// Look up 8 sinc coefficients for a given fractional delay.
// Used per-lane when building SIMD coefficient vectors.
struct TzplSincCoeffs {
    double c[8];
};

inline TzplSincCoeffs tzpl_sinc_coeffs(double frac) {
    double fidx = frac * double(TzplSincTable::SIZE);
    int idx = int(fidx);
    double t = fidx - double(idx);
    TzplSincCoeffs r;
    for (int k = 0; k < 8; k++) {
        r.c[k] = tzpl_sinc_table.coeffs[idx][k]
               + t * (tzpl_sinc_table.coeffs[idx + 1][k]
                    - tzpl_sinc_table.coeffs[idx][k]);
    }
    return r;
}

// ===========================================================================
// Scalar delay read functions -- gather from ring buffer + kernel.
// Used by non-SIMD codegen and by the SIMD splat path.
// ===========================================================================

template<typename T>
inline T tzpl_delay_none(T const* buf, uint64_t wrpos, uint64_t mask, T delay) {
    return buf[(wrpos - uint64_t(delay)) & mask];
}

template<typename T>
inline T tzpl_delay_linear(T const* buf, uint64_t wrpos, uint64_t mask, T delay) {
    uint64_t di = uint64_t(delay);
    T s0 = buf[(wrpos - di) & mask];
    T s1 = buf[(wrpos - di - 1) & mask];
    return tzpl_interp_linear(s0, s1, delay - T(di));
}

template<typename T>
inline T tzpl_delay_cubic(T const* buf, uint64_t wrpos, uint64_t mask, T delay) {
    uint64_t di = uint64_t(delay);
    T ym1 = buf[(wrpos - di + 1) & mask];
    T y0  = buf[(wrpos - di) & mask];
    T y1  = buf[(wrpos - di - 1) & mask];
    T y2  = buf[(wrpos - di - 2) & mask];
    return tzpl_interp_cubic(ym1, y0, y1, y2, delay - T(di));
}

template<typename T>
inline T tzpl_delay_lagrange(T const* buf, uint64_t wrpos, uint64_t mask, T delay) {
    uint64_t di = uint64_t(delay);
    T s0 = buf[(wrpos - di + 3) & mask];
    T s1 = buf[(wrpos - di + 2) & mask];
    T s2 = buf[(wrpos - di + 1) & mask];
    T s3 = buf[(wrpos - di) & mask];
    T s4 = buf[(wrpos - di - 1) & mask];
    T s5 = buf[(wrpos - di - 2) & mask];
    T s6 = buf[(wrpos - di - 3) & mask];
    T s7 = buf[(wrpos - di - 4) & mask];
    return tzpl_interp_lagrange(s0, s1, s2, s3, s4, s5, s6, s7, delay - T(di));
}

template<typename T>
inline T tzpl_delay_sinc(T const* buf, uint64_t wrpos, uint64_t mask, T delay) {
    uint64_t di = uint64_t(delay);
    T s0 = buf[(wrpos - di + 3) & mask];
    T s1 = buf[(wrpos - di + 2) & mask];
    T s2 = buf[(wrpos - di + 1) & mask];
    T s3 = buf[(wrpos - di) & mask];
    T s4 = buf[(wrpos - di - 1) & mask];
    T s5 = buf[(wrpos - di - 2) & mask];
    T s6 = buf[(wrpos - di - 3) & mask];
    T s7 = buf[(wrpos - di - 4) & mask];
    auto sc = tzpl_sinc_coeffs(double(delay - T(di)));
    return tzpl_interp_sinc(s0, s1, s2, s3, s4, s5, s6, s7,
                            T(sc.c[0]), T(sc.c[1]), T(sc.c[2]), T(sc.c[3]),
                            T(sc.c[4]), T(sc.c[5]), T(sc.c[6]), T(sc.c[7]));
}

// ===========================================================================
// Sample buffer read functions -- gather from flat buffer + kernel.
// Like tzpl_delay_*, but index goes forward: (di + offset) & mask.
// No wrpos -- buffers are not ring buffers.
// ===========================================================================

template<typename T>
inline T tzpl_buf_none(T const* buf, uint64_t mask, T index) {
    return buf[uint64_t(index) & mask];
}

template<typename T>
inline T tzpl_buf_linear(T const* buf, uint64_t mask, T index) {
    uint64_t di = uint64_t(index);
    T s0 = buf[di & mask];
    T s1 = buf[(di + 1) & mask];
    return tzpl_interp_linear(s0, s1, index - T(di));
}

template<typename T>
inline T tzpl_buf_cubic(T const* buf, uint64_t mask, T index) {
    uint64_t di = uint64_t(index);
    T ym1 = buf[(di - 1) & mask];
    T y0  = buf[di & mask];
    T y1  = buf[(di + 1) & mask];
    T y2  = buf[(di + 2) & mask];
    return tzpl_interp_cubic(ym1, y0, y1, y2, index - T(di));
}

template<typename T>
inline T tzpl_buf_lagrange(T const* buf, uint64_t mask, T index) {
    uint64_t di = uint64_t(index);
    T s0 = buf[(di - 3) & mask];
    T s1 = buf[(di - 2) & mask];
    T s2 = buf[(di - 1) & mask];
    T s3 = buf[di & mask];
    T s4 = buf[(di + 1) & mask];
    T s5 = buf[(di + 2) & mask];
    T s6 = buf[(di + 3) & mask];
    T s7 = buf[(di + 4) & mask];
    return tzpl_interp_lagrange(s0, s1, s2, s3, s4, s5, s6, s7, index - T(di));
}

template<typename T>
inline T tzpl_buf_sinc(T const* buf, uint64_t mask, T index) {
    uint64_t di = uint64_t(index);
    T s0 = buf[(di - 3) & mask];
    T s1 = buf[(di - 2) & mask];
    T s2 = buf[(di - 1) & mask];
    T s3 = buf[di & mask];
    T s4 = buf[(di + 1) & mask];
    T s5 = buf[(di + 2) & mask];
    T s6 = buf[(di + 3) & mask];
    T s7 = buf[(di + 4) & mask];
    auto sc = tzpl_sinc_coeffs(double(index - T(di)));
    return tzpl_interp_sinc(s0, s1, s2, s3, s4, s5, s6, s7,
                            T(sc.c[0]), T(sc.c[1]), T(sc.c[2]), T(sc.c[3]),
                            T(sc.c[4]), T(sc.c[5]), T(sc.c[6]), T(sc.c[7]));
}

#endif // tzpl_delay_interp_hpp
