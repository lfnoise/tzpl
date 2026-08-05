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
//  synthdef_builtin_ops.hpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 3/5/24.
//

#pragma once
#include "synthdef_value.hpp"

namespace synthdef {

    const f64 pi       = M_PI;
    const f64 twopi    = 2.*M_PI;
    const f64 halfSqrt = M_SQRT1_2;        // square root of one half = .7071...
    const f64 twoSqrt  = M_SQRT2;          // square root of 2
    const f64 phi      = (std::sqrt(5.)+1.)/2.; // golden ratio
    const f64 halfLog2 = .5 * M_LN2;   // Used for filter coefficients.
    const f64 e        = M_E;
    const x64 j        = x64(0,1);
    const x64 log01    = std::log(.01);
    const x64 log001   = std::log(.001);
    
    // Delay buffer creation.
    S z1(S a); // One sample delay.
    S z2(S a); // Two sample delay.
    
    // Expr creation functions.
    S constant(i64 value);
    S constant(f64 value);
    S constant(vector<i64> values);
    S constant(vector<f64> values);
    
    S constant(usize chans, i64 value = 0);
    S constant(usize chans, f64 value = 0.);
    S constant(usize chans, vector<i64> values);
    S constant(usize chans, vector<f64> values);
 
    // Creates an arithmetic series of n values from first stepping by 'step'.
    // Returns a row vector. Reshape as needed.
    template <typename T>
    S iota(usize n, T first = T(0), T step = T(1));
    extern template S iota<i64>(usize, i64, i64);
    extern template S iota<f64>(usize, f64, f64);
    
    // Creates a geometric series of n values from first growing by 'grow'.
    // Returns a row vector. Reshape as needed.
    template <typename T>
    S geo(usize n, T first, T step);
    extern template S geo<i64>(usize, i64, i64);
    extern template S geo<f64>(usize, f64, f64);
    
    // Creates an arithmetic series of n values from first to last. 
    // Returns a row vector. Reshape as needed.
    template <typename T>
    S linspace(usize n, T first, T last);
    extern template S linspace<i64>(usize, i64, i64);
    extern template S linspace<f64>(usize, f64, f64);
    
    // Creates a geometric series of n values from first to last.
    // Returns a row vector. Reshape as needed.
    S logspace(usize n, f64 first, f64 last);

   
    S fs(); // Sample rate.
    S sd(); // Sample duration.
    
    S control(string name, ControlSpec const& spec, NumType type, usize chans);
    S inlet(NumType type, usize chans, string name = "in");
    S outlet(S value, string name = "out");
    
    S urand(usize chans, SignalRate rate = audioSignalRate);
    S birand(usize chans, SignalRate rate = audioSignalRate);
    S rand64(usize chans, SignalRate rate = audioSignalRate);

    // Read one slot of the engine's shared-input table (mouse position, user
    // slots -- see tzpl_SharedInput in tzpl_plugin_abi.h). Init or audio rate.
    S sharedIn(usize slot, SignalRate rate = audioSignalRate);

    // Expr math functions.
    S abs(S a);
    S neg(S a);
    S bitNot(S a);
    S inv(S a);
    
    S clz(S a); // Count leading zeros.
    S ctz(S a); // Count trailing zeros.
    S clo(S a); // Count leading ones.
    S cto(S a); // Count trailing ones.
    S popcount(S a); // Count the number of one bits.
    S numbits(S a); // The number of bits required to represent the current value.
    
    S sqrt(S a);
    S cbrt(S a);
    S floor(S a);
    S ceil(S a);
    S round(S a);
    S trunc(S a);
    S log(S a);
    S log2(S a);
    S log10(S a);
    S log1p(S a);
    S exp(S a);
    S exp2(S a);
    S exp10(S a);
    S expm1(S a);
    //std::pair<S, S> sincos(S a);
    S sinpi(S a);
    S cospi(S a);
    S tanpi(S a);
    S sin(S a);
    S cos(S a);
    S tan(S a);
    S asin(S a);
    S acos(S a);
    S atan(S a);
    S sinh(S a);
    S cosh(S a);
    S tanh(S a);
    S asinh(S a);
    S acosh(S a);
    S atanh(S a);
    S erf(S a);
    S erfc(S a);

//    S matmul(S a, S b);

    S min(S a, S b);
    S max(S a, S b);
    S pow(S a, S b);
    S atan2(S a, S b);
    S hypot(S a, S b);
    S copysign(S a, S b);

    S operator+(S a, S b);
    S operator-(S a, S b);
    S operator*(S a, S b);
    S operator/(S a, S b);
    S operator%(S a, S b);
    S operator&(S a, S b);
    S operator|(S a, S b);
    S operator^(S a, S b);
    S operator<<(S a, S b);
    S operator>>(S a, S b);
    
    S& operator+=(S& a, S const& b);
    S& operator-=(S& a, S const& b);
    S& operator*=(S& a, S const& b);
    S& operator/=(S& a, S const& b);
    S& operator%=(S& a, S const& b);

    S& operator&=(S& a, S const& b);
    S& operator|=(S& a, S const& b);
    S& operator^=(S& a, S const& b);

    S& operator<<=(S& a, S const& b);
    S& operator>>=(S& a, S const& b);

    S eq(S a, S b);
    S ne(S a, S b);
    S lt(S a, S b);
    S le(S a, S b);
    S gt(S a, S b);
    S ge(S a, S b);

    S operator==(S a, S b);
    S operator!=(S a, S b);
    S operator<(S a, S b);
    S operator<=(S a, S b);
    S operator>(S a, S b);
    S operator>=(S a, S b);
    
    S cast_i32(S a);
    S cast_i64(S a);
    S cast_f32(S a);
    S cast_f64(S a);
        
    S sel(vector<S> inputs);
    
    template <typename... Args>
    S sel(S a, Args... args) {
        return sel({a, args...});
    }
    S sel2(S a, S ifTrue, S ifFalse);
    
    S switch_(S test, vector<std::function<S()>> inputs);
    
    template <typename... Args>
    S switch_(S test, Args ...cases) {
        return switch_(test, vector<std::function<S()>>{ {cases...} });
    }
    
    S for_(S count, std::function<S(S)> body);

    S if_(S test, std::function<S()> thenClause); // else return zero of the inferred type.
    S if_(S test, std::function<S()> thenClause, std::function<S()> elseClause);

    S reduce(S a, BinaryOp op, usize cols = 1);
    S sum(S a, usize cols = 1);
    S product(S a, usize cols = 1);
    S min(S a, usize cols = 1);
    S max(S a, usize cols = 1);
    
    S vec_take(S a, usize n);
    S vec_drop(S a, usize n);
    S vec_stride(S a, usize n);
    S vec_stutter(S a, usize n);
    S vec_ncyc(S a, usize n);
    S vec_reverse(S a);
    S vec_transpose(S a, usize n);
    S vec_rotate(S a, S n);
    S vec_at(S a, S i);
    S vec_put(S a, S i, S v);
    S vec_join(vector<S> inputs);
    
    // Helper type trait to determine if any type in a pack is floating-point
    template<typename... Ts>
    struct any_floating_point : std::disjunction<std::is_floating_point<Ts>...> {};

    template<typename... Ts>
    struct all_integral : std::conjunction<std::is_integral<Ts>...> {};

    // Variadic template function for mixed or floating-point types
    template<typename... Args,
             typename std::enable_if<any_floating_point<Args...>::value, bool>::type = true>
    S vec(Args... args) {
        vector values = {static_cast<f64>(args)...};
        return constant(std::move(values));
    }

    // Variadic template function for integral types
    template<typename... Args,
             typename std::enable_if<all_integral<Args...>::value, bool>::type = true>
    S vec(Args... args) {
        vector values = {static_cast<i64>(args)...};
        return constant(std::move(values));
    }
    
    S unary_op(S a, UnaryOp op);
    S binary_op(S a, S b, BinaryOp op);
    S compare_op(S a, S b, CompareOp op);
    S cast_op(S a, NumType type);
    S spectral_chain(S input, int fftSize, int hopSize, std::function<S(S)> body);

    S newSubgraph(std::function<S()> f);
    S newSubgraph(std::function<S(S)> f);
    
}
