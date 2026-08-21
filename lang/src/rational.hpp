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
//  tzpl_rational.hpp
//  routines
//
//  Created by James McCartney on 5/23/20.
//

#ifndef tzpl_rational_hpp
#define tzpl_rational_hpp

#include "math.hpp"


template <class T>
class rational
{
	T n, d;
public:
	
	constexpr rational() : n(0), d(1) {}
	explicit rational(T n) : n(n), d(1) {}
	rational(T n, T d) : n(n), d(d) {
		simplify();
	}
	
	constexpr rational(T n, T d, bool) : n(n), d(d) {} // no simplify
	
	rational(rational const&) = default;
	rational(rational&&) = default;

	template<class X>
	rational(rational<X> const& that) : n(T(that.numer())), d(T(that.denom())) {}

	rational& operator=(rational const&) = default;
	rational& operator=(rational&&) = default;

	template<class X>
	rational& operator=(rational<X> const& that) {
		n = that.n;
		d = that.d;
	}

	bool isInt() const noexcept { return d == 1; }
	
	T numer() const noexcept { return n; }
	T denom() const noexcept { return d; }

	void simplify() {
		if (d <= 0) {
			if (d == 0) throw std::runtime_error("rational: divide by zero");
			n = -n;
			d = -d;
		}
		if (n == 0) {
			d = 1;
		} else {
			T g = gcd(n, d);
			n /= g;
			d /= g;
		}
	}
	
	rational abs() const {
		return {::abs(n),d};
	}
	
	rational reciprocal() const {
		return {d,n};
	}
	
	operator f64() const noexcept {
		return f64(n)/f64(d);
	}

	rational operator-() const {
		return {-n, d, false};
	}
	
	rational operator+(rational b) {
		T dd = lcm(d, b.d);
		T nn = n*(dd/d) + b.n*(dd/b.d);
		return {nn, dd};
	}
	
	rational operator-(rational b) {
		T dd = lcm(d, b.d);
		T nn = n*(dd/d) - b.n*(dd/b.d);
		return {nn, dd};
	}
	
	rational operator*(rational b) {
		// do cross cancellation first
		rational aa{n,b.d};
		rational bb{b.n,d};
		aa.simplify();
		bb.simplify();
		// do product
		return {aa.n*bb.n, aa.d*bb.d, false};
	}
	
	rational operator/(rational b) {
		return *this * b.reciprocal();
	}
	
	template <class U>
	rational operator<<(U b) {
		if (b >= 0) return {n << b, d};
		else return {n >> -b, d, false};
	}
	template <class U>
	rational operator>>(U b) {
		if (b >= 0) return {n >> b, d, false};
		else return {n << -b, d};
	}
	
	rational& operator+=(rational b) {
		return *this = *this + b;
	}
	
	rational& operator-=(rational b) {
		return *this = *this - b;
	}
	
	rational& operator*=(rational b) {
		return *this = *this * b;
	}
	
	rational& operator/=(rational b) {
		return *this = *this / b;
	}
	
	rational& operator++() {
		n += d;
		return *this;
	}
	rational& operator--() {
		n -= d;
		return *this;
	}
	
	rational operator++(int) {
		rational out = *this;
		n += d;
		return out;
	}
	rational operator--(int) {
		rational out = *this;
		n -= d;
		return out;
	}
	
	template<class X>
	bool operator==(rational<X> b) {
		return n == b.numer() && d == b.denom();
	}
	template<class X>
	bool operator!=(rational<X> b) {
		return n != b.numer() || d != b.denom();
	}
	bool operator<(rational b) {
		T dd = lcm(d, b.d);
		return n*(dd/d) < b.n*(dd/b.d);
	}
	bool operator>(rational b) {
		T dd = lcm(d, b.d);
		return n*(dd/d) > b.n*(dd/b.d);
	}
	bool operator<=(rational b) {
		T dd = lcm(d, b.d);
		return n*(dd/d) <= b.n*(dd/b.d);
	}
	bool operator>=(rational b) {
		T dd = lcm(d, b.d);
		return n*(dd/d) >= b.n*(dd/b.d);
	}

	friend int cmp(rational a, rational b) {
		if (a == b) return 0;
		if (a < b) return -1;
		else return 1;
	}
	
	friend rational abs(rational a) { return { std::abs(a.n), a.d }; }
	
	friend T trunc(rational a) { return a.n / a.d; }
	friend T floor(rational a) {
		if (a.n >= 0) {
			return a.n / a.d;
		} else {
			return (a.n - a.d) / a.d;
		}
	}
	friend rational frac(rational a) { return a - rational(floor(a)); }
	friend T ceil(rational a)  { return (a.n + a.d - 1) / a.d; }
	friend T round(rational a) {
		if (a.n >= 0) {
			return (a.n + a.d/2) / a.d;
		} else {
			return (a.n + a.d/2 - a.d) / a.d;
		}
	}

	friend rational dim(rational a, rational b) {
		T dd = lcm(a.d, b.d);
		T nn = a.n*(dd/a.d) - b.n*(dd/b.d);
		if (nn <= 0) return { 0, 1, false };
		return {nn, dd, false};
	}
	

	friend rational min(rational a, rational b) { return a < b ? a : b; }
	friend rational max(rational a, rational b) { return a > b ? a : b; }
	
	friend rational pow(rational a, T b) {
		if (b < 0) {
			a = a.reciprocal();
			b = -b;
		}
		return {ipow(a.n, b), ipow(a.d, b), false}; // since already simplified, can have no common prime factors, and denom cannot be zero or negative.
	}
	
	friend rational mid(rational a, rational b) { // returns a ratio between a and b. e.g. mid(2/3, 3/4) => 5/7
		return {a.n + b.n, a.d + b.d};
	}
};

using r64 = rational<i64>;
using r32 = rational<i32>;


#include <type_traits>

namespace std {
	template<>
	struct common_type<::r64, ::r32> {
		using type = ::r64;
	};
	template<>
	struct common_type<::r32, ::r64> {
		using type = ::r64;
	};
}

#endif /* tzpl_rational_hpp */
