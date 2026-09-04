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
//  synthdef_matrix.cpp
//  synthdef-compiler
//
//  Created by James McCartney on 3/19/24.
//

#include "synthdef_matrix.hpp"
#include "synthdef_signal_type.hpp"

namespace synthdef {

template <typename T, typename F>
VectorT<T> unary_op_loop(VectorT<T> const& a, F f) {
    vector<T> new_values;
    for (usize i = 0; i < a.size(); ++i) {
        new_values.push_back(f(a[i]));
    }
    return VectorT<T>(new_values);
}

VectorT<i64> unary_op_int(VectorT<i64> const& a, UnaryOp op) {
    switch (op) {
        case UnaryOp::Neg:
            return unary_op_loop(a, [](i64 x) { return -x; });
        case UnaryOp::Not:
            return unary_op_loop(a, [](i64 x) { return ~x; });
        case UnaryOp::Abs:
            return unary_op_loop(a, [](i64 x) { return std::abs(x); });
        case UnaryOp::BitNot:
            return unary_op_loop(a, [](i64 x) { return ~x; });
        case UnaryOp::Clz:
            return unary_op_loop(a, [](i64 x) { return std::countl_zero(u64(x)); });
        case UnaryOp::Ctz:
            return unary_op_loop(a, [](i64 x) { return std::countr_zero(u64(x)); });
        case UnaryOp::PopCount:
            return unary_op_loop(a, [](i64 x) { return std::popcount(u64(x)); });
        case UnaryOp::NumBits:
            return unary_op_loop(a, [](i64 x) { return std::bit_width(u64(x)); });
        
        default:
            // Handle unsupported unary operators
            throw std::runtime_error("Unsupported unary operator for integers");
    }
}

VectorT<f64> unary_op_float(VectorT<f64> const& a, UnaryOp op) {
    switch (op) {
        case UnaryOp::Neg:
            return unary_op_loop(a, [](f64 x) { return -x; });
        case UnaryOp::Abs:
            return unary_op_loop(a, [](f64 x) { return std::abs(x); });
        case UnaryOp::Floor:
            return unary_op_loop(a, [](f64 x) { return std::floor(x); });
        case UnaryOp::Ceil:
            return unary_op_loop(a, [](f64 x) { return std::ceil(x); });
        case UnaryOp::Round:
            return unary_op_loop(a, [](f64 x) { return std::round(x); });
        case UnaryOp::Trunc:
            return unary_op_loop(a, [](f64 x) { return std::trunc(x); });
        case UnaryOp::Sqrt:
            return unary_op_loop(a, [](f64 x) { return std::sqrt(x); });
        case UnaryOp::Cbrt:
            return unary_op_loop(a, [](f64 x) { return std::cbrt(x); });
        case UnaryOp::Log:
            return unary_op_loop(a, [](f64 x) { return std::log(x); });
        case UnaryOp::Log2:
            return unary_op_loop(a, [](f64 x) { return std::log2(x); });
        case UnaryOp::Log10:
            return unary_op_loop(a, [](f64 x) { return std::log10(x); });
        case UnaryOp::Log1p:
            return unary_op_loop(a, [](f64 x) { return std::log1p(x); });
        case UnaryOp::Exp:
            return unary_op_loop(a, [](f64 x) { return std::exp(x); });
        case UnaryOp::Exp2:
            return unary_op_loop(a, [](f64 x) { return std::exp2(x); });
        case UnaryOp::Exp10:
#ifdef __APPLE__
            return unary_op_loop(a, [](f64 x) { return __exp10(x); });
#else
            return unary_op_loop(a, [](f64 x) { return std::pow(10., x); });
#endif
        case UnaryOp::Expm1:    
            return unary_op_loop(a, [](f64 x) { return std::expm1(x); });
#ifdef __APPLE__
        case UnaryOp::SinPi:
            return unary_op_loop(a, [](f64 x) { return __sinpi(x); });
        case UnaryOp::CosPi:
            return unary_op_loop(a, [](f64 x) { return __cospi(x); });
        case UnaryOp::TanPi:
            return unary_op_loop(a, [](f64 x) { return __tanpi(x); });
#else
        // Same formulas the generated code's tzpl_sinpi helpers use, so
        // constant folding matches runtime evaluation.
        case UnaryOp::SinPi:
            return unary_op_loop(a, [](f64 x) { return std::sin(x * M_PI); });
        case UnaryOp::CosPi:
            return unary_op_loop(a, [](f64 x) { return std::cos(x * M_PI); });
        case UnaryOp::TanPi:
            return unary_op_loop(a, [](f64 x) { return std::tan(x * M_PI); });
#endif
        case UnaryOp::Sin:
            return unary_op_loop(a, [](f64 x) { return std::sin(x); });
        case UnaryOp::Cos:
            return unary_op_loop(a, [](f64 x) { return std::cos(x); });
        case UnaryOp::Tan:
            return unary_op_loop(a, [](f64 x) { return std::tan(x); });
        case UnaryOp::Asin:
            return unary_op_loop(a, [](f64 x) { return std::asin(x); });
        case UnaryOp::Atan:
            return unary_op_loop(a, [](f64 x) { return std::atan(x); });
        case UnaryOp::Sinh:
            return unary_op_loop(a, [](f64 x) { return std::sinh(x); });
        case UnaryOp::Cosh:
            return unary_op_loop(a, [](f64 x) { return std::cosh(x); });
        case UnaryOp::Tanh: 
            return unary_op_loop(a, [](f64 x) { return std::tanh(x); });
        case UnaryOp::Asinh:
            return unary_op_loop(a, [](f64 x) { return std::asinh(x); });
        case UnaryOp::Atanh:
            return unary_op_loop(a, [](f64 x) { return std::atanh(x); });
        case UnaryOp::Erf:
            return unary_op_loop(a, [](f64 x) { return std::erf(x); });
        case UnaryOp::Erfc:
            return unary_op_loop(a, [](f64 x) { return std::erfc(x); });
        default:
            // Handle unsupported unary operators
            throw std::runtime_error("Unsupported unary operator for floats");
    }
}

VectorT<f64> to_float(VectorT<i64> const& a) {
    vector<f64> new_values;
    for (usize i = 0; i < a.size(); ++i) {
        new_values.push_back(static_cast<f64>(a[i]));
    }
    return VectorT<f64>(new_values);
}

VectorT<i64> to_int(VectorT<f64> const& a) {
    vector<i64> new_values;
    for (usize i = 0; i < a.size(); ++i) {
        new_values.push_back(static_cast<i64>(a[i]));
    }
    return VectorT<i64>(new_values);
}

Constant* to_float(Constant const* a) {
    return std::visit(overloaded {
        [](VectorT<i64> const& arg) { return new Constant{to_float(arg)}; },
        [](VectorT<f64> const& arg) { return new Constant{arg}; }
    }, a->value);
}

Constant* to_int(Constant const* a) {
    return std::visit(overloaded {
        [](VectorT<i64> const& arg) { return new Constant{arg}; },
        [](VectorT<f64> const& arg) { return new Constant{to_int(arg)}; }
    }, a->value);
}

#if 0
std::pair<Constant*, Constant*> sincos(Constant const* a) {
    return std::visit(overloaded {
        [](VectorT<i64> const& arg) { 
            auto argf = to_float(arg);
            vector<f64> sin_values, cos_values;
            for (usize i = 0; i < arg.size(); ++i) {
                f64 x = argf[i];
                sin_values.push_back(std::sin(x));
                cos_values.push_back(std::cos(x));
            }
            return std::make_pair(new Constant{VectorT<f64>{arg.shape, sin_values}}, new Constant{VectorT<f64>{arg.shape, cos_values}});
        },
        [](VectorT<f64> const& arg) { 
            vector<f64> sin_values, cos_values;
            for (usize i = 0; i < arg.size(); ++i) {
                f64 x = arg[i];
                sin_values.push_back(std::sin(x));
                cos_values.push_back(std::cos(x));
            }
            return std::make_pair(new Constant{VectorT<f64>{arg.shape, sin_values}}, new Constant{VectorT<f64>{arg.shape, cos_values}});
        }
    }, a->value);
}
#endif

Constant* unary_op(Constant const* a, UnaryOp op) {
    return std::visit(overloaded {
        [op](VectorT<i64> const& arg) { 
            if (is_float_unary_op(op)) {
                return new Constant{unary_op_float(to_float(arg), op)}; 
            } else {
                return new Constant{unary_op_int(arg, op)}; 
            }
        },
        [op](VectorT<f64> const& arg) { return new Constant{unary_op_float(arg, op)}; }
    }, a->value);
}

template <typename T, typename F>
VectorT<T> binary_op_loop(VectorT<T> const& a, VectorT<T> const& b, F f) {
    usize chans = broadcast(a.size(), b.size());
    vector<T> new_values;
    for (usize i = 0; i < chans; ++i) {
        new_values.push_back(f(a.at(i), b.at(i)));
    }
    return VectorT<T>{new_values};
}

VectorT<i64> binary_op_int(VectorT<i64> const& a, VectorT<i64> const& b, BinaryOp op) {
    switch (op) {
        case BinaryOp::Add:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return x + y; });
        case BinaryOp::Sub:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return x - y; });
        case BinaryOp::Mul:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return x * y; });
        case BinaryOp::Div:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return x / y; });
        case BinaryOp::Rem:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return x % y; });
        case BinaryOp::BitAnd:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return x & y; });
        case BinaryOp::BitOr:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return x | y; });
        case BinaryOp::BitXor:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return x ^ y; });
        case BinaryOp::ShiftLeft:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return x << y; });
        case BinaryOp::ShiftRight:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return x >> y; });
        case BinaryOp::UnsignedShiftRight:
            return binary_op_loop(a, b, [](i64 x, i64 y) { 
                return static_cast<u64>(x) >> static_cast<u64>(y); 
            });
        case BinaryOp::Mod:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return synthdef::mod(x, y); });
        case BinaryOp::Min:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return std::min(x, y); });
        case BinaryOp::Max:
            return binary_op_loop(a, b, [](i64 x, i64 y) { return std::max(x, y); });
        default:
            // Handle unsupported binary operators
            throw std::runtime_error("Unsupported binary operator for integers");
    }
}

VectorT<f64> binary_op_float(VectorT<f64> const& a, VectorT<f64> const& b, BinaryOp op) {
    switch (op) {
        case BinaryOp::Add:
            return binary_op_loop(a, b, [](f64 x, f64 y) { return x + y; });
        case BinaryOp::Sub:
            return binary_op_loop(a, b, [](f64 x, f64 y) { return x - y; });
        case BinaryOp::Mul:
            return binary_op_loop(a, b, [](f64 x, f64 y) { return x * y; });
        case BinaryOp::Div:
            return binary_op_loop(a, b, [](f64 x, f64 y) { return x / y; });
        case BinaryOp::Rem:
            return binary_op_loop(a, b, [](f64 x, f64 y) { return std::fmod(x, y); });
        case BinaryOp::Mod:
            return binary_op_loop(a, b, [](f64 x, f64 y) { return synthdef::mod(x, y); });
        case BinaryOp::Min:
            return binary_op_loop(a, b, [](f64 x, f64 y) { return std::min(x, y); });
        case BinaryOp::Max:
            return binary_op_loop(a, b, [](f64 x, f64 y) { return std::max(x, y); });
        case BinaryOp::Pow:
            return binary_op_loop(a, b, [](f64 x, f64 y) { return std::pow(x, y); });
        case BinaryOp::Hypot:
            return binary_op_loop(a, b, [](f64 x, f64 y) { return std::hypot(x, y); });
        case BinaryOp::Atan2:
            return binary_op_loop(a, b, [](f64 x, f64 y) { return std::atan2(x, y); });
        case BinaryOp::Copysign:
            return binary_op_loop(a, b, [](f64 x, f64 y) { return std::copysign(x, y); });
        default:
            // Handle unsupported binary operators
            throw std::runtime_error("Unsupported binary operator for floats");
    }
}

Constant* binary_op(Constant const* a, Constant const* b, BinaryOp op) {
    return std::visit(overloaded {
        [op](VectorT<i64> const& arg_a, VectorT<i64> const& arg_b) { 
            if (is_float_binary_op(op)) {
                return new Constant{binary_op_float(to_float(arg_a), to_float(arg_b), op)}; 
            } else {
                return new Constant{binary_op_int(arg_a, arg_b, op)}; 
            }
        },
        [op](VectorT<i64> const& arg_a, VectorT<f64> const& arg_b) { 
            return new Constant{binary_op_float(to_float(arg_a), arg_b, op)}; 
        },
        [op](VectorT<f64> const& arg_a, VectorT<i64> const& arg_b) { 
            return new Constant{binary_op_float(arg_a, to_float(arg_b), op)}; 
        },
        [op](VectorT<f64> const& arg_a, VectorT<f64> const& arg_b) { 
            return new Constant{binary_op_float(arg_a, arg_b, op)}; 
        }
    }, a->value, b->value);
}

template <typename T, typename F>
VectorT<i64> compare_op_loop(VectorT<T> const& a, VectorT<T> const& b, F f) {
    usize chans = broadcast(a.size(), b.size());
    vector<i64> new_values;
    for (usize i = 0; i < chans; ++i) {
        new_values.push_back(f(a.at(i), b.at(i)));
    }
    return VectorT<i64>{new_values};
}

VectorT<i64> compare_op_int(VectorT<i64> const& a, VectorT<i64> const& b, CompareOp op) {
    switch (op) {
        case CompareOp::EQ:
            return compare_op_loop(a, b, [](i64 x, i64 y) { return x == y ? -1 : 0; });
        case CompareOp::NE:
            return compare_op_loop(a, b, [](i64 x, i64 y) { return x != y ? -1 : 0; });
        case CompareOp::LE:
            return compare_op_loop(a, b, [](i64 x, i64 y) { return x <= y ? -1 : 0; });
        case CompareOp::LT:
            return compare_op_loop(a, b, [](i64 x, i64 y) { return x < y ? -1 : 0; });
        case CompareOp::GE:
            return compare_op_loop(a, b, [](i64 x, i64 y) { return x >= y ? -1 : 0; });
        case CompareOp::GT:
            return compare_op_loop(a, b, [](i64 x, i64 y) { return x > y ? -1 : 0; });
        default:
            // Handle unsupported compare operators
            throw std::runtime_error("Unsupported compare operator for integers");
    }
}

VectorT<i64> compare_op_float(VectorT<f64> const& a, VectorT<f64> const& b, CompareOp op) {
    switch (op) {
        case CompareOp::EQ:
            return compare_op_loop(a, b, [](f64 x, f64 y) { return x == y ? -1 : 0; });
        case CompareOp::NE:
            return compare_op_loop(a, b, [](f64 x, f64 y) { return x != y ? -1 : 0; });
        case CompareOp::LE:
            return compare_op_loop(a, b, [](f64 x, f64 y) { return x <= y ? -1 : 0; });
        case CompareOp::LT:
            return compare_op_loop(a, b, [](f64 x, f64 y) { return x < y ? -1 : 0; });
        case CompareOp::GE:
            return compare_op_loop(a, b, [](f64 x, f64 y) { return x >= y ? -1 : 0; });
        case CompareOp::GT:
            return compare_op_loop(a, b, [](f64 x, f64 y) { return x > y ? -1 : 0; });
        default:
            // Handle unsupported compare operators
            throw std::runtime_error("Unsupported compare operator for floats");
    }
}

Constant* compare_op(Constant const* a, Constant const* b, CompareOp op) {
    return std::visit(overloaded {
        [op](VectorT<i64> const& arg_a, VectorT<i64> const& arg_b) { 
            return new Constant{compare_op_int(arg_a, arg_b, op)}; 
        },
        [op](VectorT<i64> const& arg_a, VectorT<f64> const& arg_b) { 
            return new Constant{compare_op_float(to_float(arg_a), arg_b, op)}; 
        },
        [op](VectorT<f64> const& arg_a, VectorT<i64> const& arg_b) { 
            return new Constant{compare_op_float(arg_a, to_float(arg_b), op)}; 
        },
        [op](VectorT<f64> const& arg_a, VectorT<f64> const& arg_b) { 
            return new Constant{compare_op_float(arg_a, arg_b, op)}; 
        }
    }, a->value, b->value);
}

VectorT<i64> reduce_int(VectorT<i64> const& a, usize cols, BinaryOp op) {
    switch (op) {
        case BinaryOp::Add:
            return a.reduce(cols, [](i64 x, i64 y) { return x + y; });
        case BinaryOp::Mul:
            return a.reduce(cols, [](i64 x, i64 y) { return x * y; });
        case BinaryOp::BitAnd:
            return a.reduce(cols, [](i64 x, i64 y) { return x & y; });
        case BinaryOp::BitOr:
            return a.reduce(cols, [](i64 x, i64 y) { return x | y; });
        case BinaryOp::BitXor:
            return a.reduce(cols, [](i64 x, i64 y) { return x ^ y; });
        case BinaryOp::Min:
            return a.reduce(cols, [](i64 x, i64 y) { return std::min(x, y); });
        case BinaryOp::Max:
            return a.reduce(cols, [](i64 x, i64 y) { return std::max(x, y); });
        default:
            // Handle unsupported binary operators
            throw std::runtime_error("reduce: Unsupported binary operator for integers");
    }
}

VectorT<f64> reduce_float(VectorT<f64> const& a, usize cols, BinaryOp op) {
    switch (op) {
        case BinaryOp::Add:
            return a.reduce(cols, [](f64 x, f64 y) { return x + y; });
        case BinaryOp::Mul:
            return a.reduce(cols, [](f64 x, f64 y) { return x * y; });
        case BinaryOp::Min:
            return a.reduce(cols, [](f64 x, f64 y) { return std::min(x, y); });
        case BinaryOp::Max:
            return a.reduce(cols, [](f64 x, f64 y) { return std::max(x, y); });
        default:
            // Handle unsupported binary operators
            throw std::runtime_error("reduce: Unsupported binary operator for floats");
    }
}

Constant* reduce(Constant const* a, usize cols, BinaryOp op) {
    return std::visit(overloaded {
        [cols, op](VectorT<i64> const& arg_a) { 
            return new Constant{reduce_int(arg_a, cols, op)}; 
        },
        [cols, op](VectorT<f64> const& arg_a) { 
            return new Constant{reduce_float(arg_a, cols, op)}; 
        }
    }, a->value);
}

#if 0
VectorT<i64> scan_int(VectorT<i64> const& a, usize cols, BinaryOp op) {
    switch (op) {
        case BinaryOp::Add:
            return a.scan(cols, [](i64 x, i64 y) { return x + y; });
        case BinaryOp::Mul:
            return a.scan(cols, [](i64 x, i64 y) { return x * y; });
        case BinaryOp::BitAnd:
            return a.scan(cols, [](i64 x, i64 y) { return x & y; });
        case BinaryOp::BitOr:
            return a.scan(cols, [](i64 x, i64 y) { return x | y; });
        case BinaryOp::BitXor:
            return a.scan(cols, [](i64 x, i64 y) { return x ^ y; });
        case BinaryOp::Min:
            return a.scan(cols, [](i64 x, i64 y) { return std::min(x, y); });
        case BinaryOp::Max:
            return a.scan(cols, [](i64 x, i64 y) { return std::max(x, y); });
        default:
            // Handle unsupported binary operators
            throw std::runtime_error("scan: Unsupported binary operator for integers");
    }
}

VectorT<f64> scan_float(VectorT<f64> const& a, usize cols, BinaryOp op) {
    switch (op) {
        case BinaryOp::Add:
            return a.scan(cols, [](f64 x, f64 y) { return x + y; });
        case BinaryOp::Mul:
            return a.scan(cols, [](f64 x, f64 y) { return x * y; });
        case BinaryOp::Min:
            return a.scan(cols, [](f64 x, f64 y) { return std::min(x, y); });
        case BinaryOp::Max:
            return a.scan(cols, [](f64 x, f64 y) { return std::max(x, y); });
        default:
            // Handle unsupported binary operators
            throw std::runtime_error("scan: Unsupported binary operator for floats");
    }
}

Constant* scan(Constant const* a, usize cols, BinaryOp op) {
    return std::visit(overloaded {
        [cols, op](VectorT<i64> const& arg_a) {
            return new Constant{scan_int(arg_a, cols, op)};
        },
        [cols, op](VectorT<f64> const& arg_a) {
            return new Constant{scan_float(arg_a, cols, op)};
        }
    }, a->value);
}
#endif

Constant* Constant::ushr(Constant const* that) const { 
    return binary_op(this, that, BinaryOp::UnsignedShiftRight); 
}

Constant* Constant::abs() const { 
    return unary_op(this, UnaryOp::Abs); 
}
Constant* Constant::clz() const { 
    return unary_op(this, UnaryOp::Clz); 
}
Constant* Constant::ctz() const { 
    return unary_op(this, UnaryOp::Ctz); 
}
Constant* Constant::popcount() const { 
    return unary_op(this, UnaryOp::PopCount); 
}
Constant* Constant::numbits() const { 
    return unary_op(this, UnaryOp::NumBits); 
}
Constant* Constant::floor() const { 
    return unary_op(this, UnaryOp::Floor); 
}
Constant* Constant::ceil() const { 
    return unary_op(this, UnaryOp::Ceil); 
}
Constant* Constant::round() const { 
    return unary_op(this, UnaryOp::Round); 
}
Constant* Constant::trunc() const { 
    return unary_op(this, UnaryOp::Trunc); 
}
Constant* Constant::sqrt() const { 
    return unary_op(this, UnaryOp::Sqrt); 
}
Constant* Constant::cbrt() const { 
    return unary_op(this, UnaryOp::Cbrt); 
}
Constant* Constant::log() const { 
    return unary_op(this, UnaryOp::Log); 
}
Constant* Constant::log2() const { 
    return unary_op(this, UnaryOp::Log2); 
}
Constant* Constant::log10() const { 
    return unary_op(this, UnaryOp::Log10); 
}
Constant* Constant::log1p() const { 
    return unary_op(this, UnaryOp::Log1p); 
}
Constant* Constant::exp() const { 
    return unary_op(this, UnaryOp::Exp); 
}
Constant* Constant::exp2() const { 
    return unary_op(this, UnaryOp::Exp2); 
}
Constant* Constant::exp10() const { 
    return unary_op(this, UnaryOp::Exp10); 
}
Constant* Constant::expm1() const { 
    return unary_op(this, UnaryOp::Expm1); 
}

Constant* Constant::sin() const { 
    return unary_op(this, UnaryOp::Sin); 
}
Constant* Constant::cos() const { 
    return unary_op(this, UnaryOp::Cos); 
}
Constant* Constant::tan() const { 
    return unary_op(this, UnaryOp::Tan); 
}
Constant* Constant::asin() const { 
    return unary_op(this, UnaryOp::Asin); 
}
Constant* Constant::acos() const { 
    return unary_op(this, UnaryOp::Acos); 
}
Constant* Constant::atan() const { 
    return unary_op(this, UnaryOp::Atan); 
}
Constant* Constant::sinh() const { 
    return unary_op(this, UnaryOp::Sinh); 
}
Constant* Constant::cosh() const { 
    return unary_op(this, UnaryOp::Cosh); 
}
Constant* Constant::tanh() const { 
    return unary_op(this, UnaryOp::Tanh); 
}
Constant* Constant::asinh() const { 
    return unary_op(this, UnaryOp::Asinh); 
}
Constant* Constant::acosh() const { 
    return unary_op(this, UnaryOp::Acosh); 
}
Constant* Constant::atanh() const { 
    return unary_op(this, UnaryOp::Atanh); 
}
Constant* Constant::erf() const { 
    return unary_op(this, UnaryOp::Erf); 
}
Constant* Constant::erfc() const { 
    return unary_op(this, UnaryOp::Erfc); 
}

Constant* Constant::pow(Constant const* that) const { 
    return binary_op(this, that, BinaryOp::Pow); 
}
Constant* Constant::atan2(Constant const* that) const { 
    return binary_op(this, that, BinaryOp::Atan2); 
}
Constant* Constant::hypot(Constant const* that) const { 
    return binary_op(this, that, BinaryOp::Hypot); 
}
Constant* Constant::copysign(Constant const* that) const { 
    return binary_op(this, that, BinaryOp::Copysign); 
}

Constant* Constant::eq(Constant const* that) const { 
    return compare_op(this, that, CompareOp::EQ); 
}
Constant* Constant::ne(Constant const* that) const { 
    return compare_op(this, that, CompareOp::NE); 
}
Constant* Constant::lt(Constant const* that) const { 
    return compare_op(this, that, CompareOp::LT); 
}
Constant* Constant::le(Constant const* that) const { 
    return compare_op(this, that, CompareOp::LE); 
}
Constant* Constant::gt(Constant const* that) const { 
    return compare_op(this, that, CompareOp::GT); 
}
Constant* Constant::ge(Constant const* that) const { 
    return compare_op(this, that, CompareOp::GE); 
}

string scalar_string(Constant const* a) {
    return std::visit(overloaded {
        [](VectorT<i64> const& arg) { return synthdef::ftos(arg.at(0)); },
        [](VectorT<f64> const& arg) { return synthdef::ftos(arg.at(0)); }
    }, a->value);
}

#if 0
template <typename T>
VectorT<T> matrix_multiply(VectorT<T> const& a, VectorT<T> const& b) {
    VectorT<T> c({a.shape.rows, b.shape.cols}, T(0));
    for (usize i = 0; i < a.shape.rows; ++i) {
        for (usize j = 0; j < b.shape.cols; ++j) {
            T sum = T(0);
            for (usize k = 0; k < a.shape.cols; ++k) {
                sum += a.at(i, k) * b.at(k, j);
            }
            c.at(i, j) = sum;
        }
    }
    return c;
}

Constant* matrix_multiply(Constant const* a, Constant const* b) {
    if (a->shape.cols != b->shape.rows) {
        throw std::runtime_error("matrix shapes are not compatible for matrix multiplication.");
    }
    return std::visit(overloaded {
        [](VectorT<i64> const& arg_a, VectorT<i64> const& arg_b) { 
            return new Constant{matrix_multiply(arg_a, arg_b)}; 
        },
        [](VectorT<i64> const& arg_a, VectorT<f64> const& arg_b) { 
            return new Constant{matrix_multiply(to_float(arg_a), arg_b)}; 
        },
        [](VectorT<f64> const& arg_a, VectorT<i64> const& arg_b) { 
            return new Constant{matrix_multiply(arg_a, to_float(arg_b))}; 
        },
        [](VectorT<f64> const& arg_a, VectorT<f64> const& arg_b) { 
            return new Constant{matrix_multiply(arg_a, arg_b)}; 
        }
    }, a->value, b->value);
                
}

Constant* Constant::permute(Constant const* index, Axis axis) const { 
    return std::visit(overloaded {
        [&](VectorT<i64> const& arg_a, VectorT<i64> const& arg_b) { 
            return new Constant{arg_a.permute(arg_b, axis)}; },
        [&](VectorT<i64> const& arg_a, VectorT<f64> const& arg_b) { 
            return new Constant{arg_a.permute(to_int(arg_b), axis)}; },
        [&](VectorT<f64> const& arg_a, VectorT<i64> const& arg_b) { 
            return new Constant{arg_a.permute(arg_b, axis)}; },
        [&](VectorT<f64> const& arg_a, VectorT<f64> const& arg_b) { 
            return new Constant{arg_a.permute(to_int(arg_b), axis)}; }
    }, value, index->value);
}

Constant* Constant::at(Constant const* that) const { 
    return std::visit(overloaded {
        [](VectorT<i64> const& arg_a, VectorT<i64> const& arg_b) { 
            return new Constant{arg_a.at(arg_b)}; },
        [](VectorT<i64> const& arg_a, VectorT<f64> const& arg_b) { 
            return new Constant{arg_a.at(arg_b)}; },
        [](VectorT<f64> const& arg_a, VectorT<i64> const& arg_b) { 
            return new Constant{arg_a.at(arg_b)}; },
        [](VectorT<f64> const& arg_a, VectorT<f64> const& arg_b) { 
            return new Constant{arg_a.at(arg_b)}; }
    }, value, that->value);
}

Constant* Constant::put(Constant const* index, Constant const* values) const { 
    return std::visit(overloaded {
        [&](VectorT<i64> const& arg_a, VectorT<i64> const& arg_b, VectorT<i64> const& arg_c) { 
            return new Constant{arg_a.put(arg_b, arg_c)}; },
            
        [&](VectorT<i64> const& arg_a, VectorT<i64> const& arg_b, VectorT<f64> const& arg_c) { 
            return new Constant{arg_a.put(arg_b, to_int(arg_c))}; },
            
        [&](VectorT<i64> const& arg_a, VectorT<f64> const& arg_b, VectorT<i64> const& arg_c) { 
            return new Constant{arg_a.put(to_int(arg_b), arg_c)}; },
            
        [&](VectorT<i64> const& arg_a, VectorT<f64> const& arg_b, VectorT<f64> const& arg_c) { 
            return new Constant{arg_a.put(to_int(arg_b), to_int(arg_c))}; },
            
        [&](VectorT<f64> const& arg_a, VectorT<i64> const& arg_b, VectorT<i64> const& arg_c) { 
            return new Constant{arg_a.put(arg_b, to_float(arg_c))}; },
            
        [&](VectorT<f64> const& arg_a, VectorT<i64> const& arg_b, VectorT<f64> const& arg_c) { 
            return new Constant{arg_a.put(arg_b, arg_c)}; },
            
        [&](VectorT<f64> const& arg_a, VectorT<f64> const& arg_b, VectorT<i64> const& arg_c) { 
            return new Constant{arg_a.put(to_int(arg_b), to_float(arg_c))}; },
            
        [&](VectorT<f64> const& arg_a, VectorT<f64> const& arg_b, VectorT<f64> const& arg_c) { 
            return new Constant{arg_a.put(to_int(arg_b), arg_c)}; }
    }, value, index->value, values->value);
}
#endif

} // namespace synthdef
