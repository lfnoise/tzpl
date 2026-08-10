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
//  synthdef_value.hpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 2/25/24.
//

#pragma once

#include "synthdef_signal_type.hpp"
#include "synthdef_math_ops.hpp"

namespace synthdef {

    struct Expr;
    enum Interpolation : int;

    // S represents a signal value and contains a pointer to the Expr that calculates the value. 
    // Since it is the most common type, a one character name is used.
    class S {
        // Exprs are arena allocated, so we use a raw pointer to refer to them.
        Expr* expr;
    public:
        S() : expr(nullptr) {}
        S(Expr* expr) : expr(expr) {}
        S(i32 x);
        S(f32 x);
        S(i64 x);
        S(f64 x);
        S(vector<i64> xs);
        S(vector<f64> xs);

        Expr* get() const { return expr; }

        Expr* operator->() { return expr; }
        Expr const* operator->() const { return expr; }
        Expr& operator*() { return *expr; }
        Expr const& operator*() const { return *expr; }

        bool isNull() const { return expr == nullptr; }
        bool notNull() const { return expr != nullptr; }
        
        bool equals(S that) const;
        bool identical(S that) const;
        
        template <class T> 
        T* as() const { return dynamic_cast<T*>(expr); }
    
        // Unary operators.
        S operator-() const;
        S operator~() const;
        S operator!() const;

        // Binary operator.
        S ushr(S that) const;

        // Element-wise unary functions.
        S abs() const;
        S neg() const;
        S bitNot() const;
        S inv() const; // reciprocal
        S clz() const;
        S ctz() const;
        S clo() const;
        S cto() const;
        S popcount() const;
        S numbits() const;
        S sqrt() const;
        S cbrt() const;
        S log() const;
        S log2() const;
        S log10() const;
        S log1p() const;
        S exp() const;
        S exp2() const;
        S exp10() const;
        S expm1() const;
        S floor() const;
        S ceil() const;
        S round() const;
        S trunc() const;
//        std::pair<S, S> sincos() const;
        S sinpi() const;
        S cospi() const;
        S tanpi() const;
        S sin() const;
        S cos() const;
        S tan() const;
        S asin() const;
        S acos() const;
        S atan() const;
        S sinh() const;
        S cosh() const;
        S tanh() const;
        S asinh() const;
        S acosh() const;
        S atanh() const;
        S erf() const;
        S erfc() const;

        // Matrix multiplication
//        S matmul(S b) const;
        
        // Element-wise two argument functions.
        S mod(S that) const;
        S min(S that) const;
        S max(S that) const;
        S pow(S that) const;
        S atan2(S that) const;
        S hypot(S that) const;
        S copysign(S that) const;

        // Element-wise comparison operations.
        S eq(S that) const;
        S ne(S that) const;
        S lt(S that) const;
        S le(S that) const;
        S gt(S that) const;
        S ge(S that) const;

#if 0
        // Channel operations.
        S reverse(usize cols = 1) const;
        S take(usize n, usize cols = 1) const;
        S skip(usize n, usize cols = 1) const;
        S stride(usize n, usize cols = 1) const;
        S stutter(usize n, usize cols = 1) const;
        S cyc(usize n, usize cols = 1) const;
        S rotate(S n, usize cols = 1) const;
        S transpose(usize cols) const;
        S at(S i) const;
        S put(S i, S values) const;

#endif
        S reduce(BinaryOp op, usize cols = 1) const;

        // Casts
        S cast_i32() const;
        S cast_i64() const;
        S cast_f32() const;
        S cast_f64() const;
                
        template <typename F>
        S chain(usize n, F f) const {
            S x = *this;
            for (usize i = 0; i < n; ++i) {
                x = f(x);
            }
            return x;
        }
    };

    struct ExprHasher {
        std::size_t operator()(S expr) const;
    };
    
    struct ExprEquals {
        bool operator()(S const& a, S const& b) const;
    };

    struct ExprIdentical {
        bool operator()(S const& a, S  const& b) const;
    };

    struct ExprIdentityHasher {
        std::size_t operator()(S expr) const;
    };

    using ExprSet = unordered_set<S, ExprHasher, ExprEquals>;
    using ExprIdentitySet = unordered_set<S, ExprIdentityHasher, ExprIdentical>;

    // Deterministic worklist for the shape/type inference fixpoints: LIFO
    // stack with an in-queue membership set. Hash-set iteration order must
    // not leak into inference order (and thus codegen) -- synthc's WorkList
    // mirrors this exact push/pop discipline, and the two compilers must
    // resolve inference ties identically to stay byte-identical.
    struct ExprWorkList {
        vector<S> items;
        ExprIdentitySet inq;

        void insert(S expr) {
            if (inq.insert(expr).second) items.push_back(expr);
        }
        bool empty() const { return items.empty(); }
        S pop() {
            S expr = items.back();
            items.pop_back();
            inq.erase(expr);
            return expr;
        }
    };
    
class ExprIdentityBag {
    unordered_map<S, usize, ExprIdentityHasher, ExprIdentical> map;
    usize total_ = 0;
public:
    auto begin() const { return map.cbegin(); }
    auto end() const { return map.cend(); }

private:
    template <typename IteratorType>
    struct ExprIterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = S;
        using difference_type = usize;
        using pointer = S*;
        using reference = S&;

        IteratorType i;

        ExprIterator(IteratorType i) : i(i) {}

        ExprIterator const& operator++() { ++i; return *this; }
        ExprIterator operator++(int) { auto j = *this; ++i; return j; }
        bool operator==(ExprIterator that) const { return i == that.i; }
        bool operator!=(ExprIterator that) const { return i != that.i; }
        S operator*() const { return i->first; }
    };

    struct Exprs {
        ExprIdentityBag const& bag;

        Exprs(ExprIdentityBag const& bag) : bag(bag) {}
        auto begin() const { return ExprIterator<decltype(bag.map.cbegin())>(bag.map.cbegin()); }
        auto end() const { return ExprIterator<decltype(bag.map.cend())>(bag.map.cend()); }
    };
public:
    ExprIdentityBag() {}

    bool empty() const { return map.empty(); }
    usize total() const { return total_; }
    usize count(S expr) const {
        auto i = map.find(expr);
        return i == map.end() ? 0 : i->second;
    }
    usize insert(S expr) {
        ++total_;
        return ++map[expr];
    }
    void clear() {
        map.clear();
        total_ = 0;
    }
    bool contains(S expr) const {
        return map.find(expr) != map.end();
    }
    usize erase(S expr) {
        auto i = map.find(expr);
        if (i == map.end()) return 0;
        --total_;
        if (--i->second == 0) {
            map.erase(i);
            return 0;
        }
        return i->second;
    }
    Exprs exprs() const { return Exprs(*this); }
};


    struct DelayBuf;
    
    // D is a DelayBuf reference type.
    // Since it is also a very common type, a one character name is used.
    class D {
        DelayBuf* delayBuf;
    public:
        D();
        D(S maxDelay);
        D(DelayBuf* delayBuf);
        D(D const& other);  // Copy constructor

        DelayBuf* get() const { return delayBuf; }

        DelayBuf* operator->() { return delayBuf; }
        DelayBuf const* operator->() const { return delayBuf; }
        DelayBuf& operator*() { return *delayBuf; }
        DelayBuf const& operator*() const { return *delayBuf; }

        bool operator==(D that) const { return delayBuf == that.delayBuf; }
        bool operator!=(D that) const { return delayBuf != that.delayBuf; }
        
        bool isNull() const { return delayBuf == nullptr; }
        bool notNull() const { return delayBuf != nullptr; }
        
        
        S init(usize sampleIndex, S value);
        S write(S in);
        S operator=(S in);
        
        S read(usize delaySamples) const;
        S vread(S delaySamples, Interpolation interp = Interpolation(0)) const;
        S operator()(usize delaySamples) const;
        S operator[](usize delaySamples) const;
        S v(S delaySamples, Interpolation interp = Interpolation(0)) const;
        S setMaxDelay(S maxDelay);
        
        operator S();
        
        D& operator+=(S in);
        D& operator-=(S in);
        D& operator*=(S in);
        D& operator/=(S in);
        D& operator%=(S in);
        D& operator&=(S in);
        D& operator|=(S in);
        D& operator^=(S in);
        D& operator>>=(S in);
        D& operator<<=(S in);
        
        u64 hash() const;
        
    };
    
    struct DelayHasher {
        std::size_t operator()(D delayBuf) const;
    };

    struct SampleBuf;

    // B is a SampleBuf reference type, parallel to D for DelayBuf.
    class B {
        SampleBuf* sampleBuf;
    public:
        B();
        B(SampleBuf* sampleBuf);
        B(B const& other);

        SampleBuf* get() const { return sampleBuf; }

        SampleBuf* operator->() { return sampleBuf; }
        SampleBuf const* operator->() const { return sampleBuf; }

        bool operator==(B that) const { return sampleBuf == that.sampleBuf; }
        bool operator!=(B that) const { return sampleBuf != that.sampleBuf; }

        bool isNull() const { return sampleBuf == nullptr; }
        bool notNull() const { return sampleBuf != nullptr; }

        S read(i64 index, i64 readChans = 1, i64 startChan = 0) const;
        S vread(S index, Interpolation interp = Interpolation(0),
                i64 readChans = 1, i64 startChan = 0) const;
        S write(S value, S index, i64 writeChans = 0, i64 startChan = 0);
        S length() const;

        u64 hash() const;
    };

    struct SampleBufHasher {
        std::size_t operator()(B buf) const;
    };

} // namespace synthdef

