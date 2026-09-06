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

/*
 *  tzpl_sinpi.hpp
 *
 *  Scalar sinpi / cospi / tanpi / exp10, identical on every platform.
 *
 *  These used to map to Apple's libm extensions (__sinpi and friends) on
 *  macOS and to sin(x * pi) elsewhere, which made golden output differ per
 *  OS. This implementation reduces the argument to a quadrant with exact
 *  floating-point operations first, so integer and half-integer arguments
 *  give exact 0 / +-1 everywhere, then evaluates one std::sin on a small
 *  argument. Shared by the interpreter (lang/src/math.hpp), the synthdef
 *  compiler's constant folder, and generated plugin code (via
 *  tzpl_matrix_transform.hpp), which keeps constant folding and runtime
 *  evaluation bit-identical.
 */

#ifndef tzpl_sinpi_hpp
#define tzpl_sinpi_hpp

#include <cmath>
#include <limits>
#include <numbers>

namespace synthdef {

template <typename T>
inline T tzpl_sinpi_impl(T x) {
    if (!std::isfinite(x)) return std::numeric_limits<T>::quiet_NaN();
    T r = std::fmod(x, T(2));                       // exact, r in (-2, 2)
    if (r > T(1)) r -= T(2); else if (r < T(-1)) r += T(2);          // exact, r in [-1, 1]
    if (r > T(0.5)) r = T(1) - r; else if (r < T(-0.5)) r = T(-1) - r; // exact, r in [-0.5, 0.5]
    return std::sin(r * std::numbers::pi_v<T>);
}

template <typename T>
inline T tzpl_cospi_impl(T x) {
    if (!std::isfinite(x)) return std::numeric_limits<T>::quiet_NaN();
    T r = std::fabs(std::fmod(x, T(2)));            // exact, r in [0, 2)
    if (r > T(1)) r = T(2) - r;                     // exact, r in [0, 1]
    // cos(pi r) == sin(pi (1/2 - r)), and 1/2 - r is in [-1/2, 1/2].
    return std::sin((T(0.5) - r) * std::numbers::pi_v<T>);
}

inline float  tzpl_sinpi(float x)  { return tzpl_sinpi_impl(x); }
inline double tzpl_sinpi(double x) { return tzpl_sinpi_impl(x); }
inline float  tzpl_cospi(float x)  { return tzpl_cospi_impl(x); }
inline double tzpl_cospi(double x) { return tzpl_cospi_impl(x); }
inline float  tzpl_tanpi(float x)  { return tzpl_sinpi(x) / tzpl_cospi(x); }
inline double tzpl_tanpi(double x) { return tzpl_sinpi(x) / tzpl_cospi(x); }
// std::exp10 does not exist in any standard library; pow(10, x) is exact
// for the integer arguments that matter.
inline float  tzpl_exp10(float x)  { return std::pow(10.f, x); }
inline double tzpl_exp10(double x) { return std::pow(10., x); }

} // namespace synthdef

#endif // tzpl_sinpi_hpp
