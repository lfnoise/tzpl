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
//  tzpl_fade_curves.hpp
//  audio engine
//
//  The crossfade curves, shared by XFader nodes and input-stage ramps, and
//  the fade position function both use.
//
//  A fade (or ramp) of `count` samples starting at fractional sample time t0
//  gives sample time t the position
//
//      k = t - t0;   x = k < 0 ? 0 : k >= count ? 1 : k / (count + 1)
//
//  For an integer t0 this is exactly the per-sample engine's fader, which
//  stepped x by 1/(count+1) for `count` samples and then handed over to the
//  new source. A fractional t0 starts the fade between samples. count 0 is a
//  step at t0 (the first sample at or after t0 takes the new value).
//

#ifndef tzpl_fade_curves_hpp
#define tzpl_fade_curves_hpp

#include <cmath>
#include <concepts>
#include <cstdint>

namespace engine {

enum FadeCurve {
    fadeLinear,
    fadeExponential,
    fadeSmoothstep,
    fadeEqualPower,
    fadeOutIn,
    fadeEaseInCubic,
    fadeEaseOutCubic,
};

inline double fadePosition(double t, double t0, int64_t count) {
    double k = t - t0;
    if (k < 0.) return 0.;
    if (k >= (double)count) return 1.;
    return k / (double)(count + 1);
}

// True once every sample from t on has position 1.
inline bool fadeDoneAfter(double t, double t0, int64_t count) {
    return t + 1. - t0 >= (double)count;
}

template <std::floating_point T>
T smoothStep(T x) {
    return x * x * (T(3) - T(2) * x);
}

template <std::floating_point T>
T panfun(T x) {
    static constexpr T a = 0.337403011047526069;
    static constexpr T b = -1.334338069017510398;
    static constexpr T c = -a-b-T(1); // a+b+c = -1.
    return T(1) + x * (c + x * (b + x * a));
}

template <std::floating_point T>
T cubed(T x) { return x*x*x; }

template <std::floating_point T>
struct LinearFade {
    static T fade(T x, T a, T b) { return a + x * (b - a); }
};

template <std::floating_point T>
struct ExponentialFade {
    static T fade(T x, T a, T b) { return a * std::pow(b/a, x); }
};

template <std::floating_point T>
struct SmoothStepFade {
    static T fade(T x, T a, T b) { return a + smoothStep(x) * (b - a); }
};

template <std::floating_point T>
struct OutInFade {
    static T fade(T x, T a, T b) {
        return x < 0.5 ? smoothStep(T(1) - T(2)*x) * a : smoothStep(T(2)*x - T(1)) * b;
    }
};

template <std::floating_point T>
struct EqPowFade {
    // panfun is a fade-out gain (panfun(0) = 1, panfun(1) = 0), so the
    // outgoing signal takes panfun(x) and the incoming one panfun(1 - x).
    static T fade(T x, T a, T b) { return a * panfun(x) + b * panfun(T(1) - x); }
};

template <std::floating_point T>
struct EaseInCubicFade {
    static T fade(T x, T a, T b) { return a + cubed(x) * (b - a); }
};

template <std::floating_point T>
struct EaseOutCubicFade {
    static T fade(T x, T a, T b) { return b + cubed(T(1) - x) * (a - b); }
};

// Runtime-dispatched form, for ramps (one value per sample, not per channel
// per sample, so the switch is cheap there).
template <std::floating_point T>
T fadeValue(FadeCurve curve, T x, T a, T b) {
    // The endpoints are exact whatever the curve, so a ramp lands on its
    // target value.
    if (x <= T(0)) return a;
    if (x >= T(1)) return b;
    switch (curve) {
        case fadeLinear:       return LinearFade<T>::fade(x, a, b);
        case fadeExponential:  return ExponentialFade<T>::fade(x, a, b);
        case fadeSmoothstep:   return SmoothStepFade<T>::fade(x, a, b);
        case fadeEqualPower:   return EqPowFade<T>::fade(x, a, b);
        case fadeOutIn:        return OutInFade<T>::fade(x, a, b);
        case fadeEaseInCubic:  return EaseInCubicFade<T>::fade(x, a, b);
        case fadeEaseOutCubic: return EaseOutCubicFade<T>::fade(x, a, b);
    }
    return b;
}

} // namespace engine

#endif // tzpl_fade_curves_hpp
