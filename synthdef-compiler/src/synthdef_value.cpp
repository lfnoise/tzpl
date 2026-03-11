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
//  synthdef_value.cpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 2/25/24.
//

#include "synthdef_value.hpp"
#include "synthdef_synth.hpp"
#include "synthdef_builtin_ops.hpp"
#include "synthdef_hash.hpp"

namespace synthdef {

    S::S(i32 x) : expr(addExpr(new Constant(i64(x))).expr) {}
    S::S(f32 x) : expr(addExpr(new Constant(f64(x))).expr) {}
    S::S(i64 x) : expr(addExpr(new Constant(x)).expr) {}
    S::S(f64 x) : expr(addExpr(new Constant(x)).expr) {}
    S::S(vector<i64> xs) : expr(addExpr(new Constant(std::move(xs))).expr) {}
    S::S(vector<f64> xs) : expr(addExpr(new Constant(std::move(xs))).expr) {}

    bool S::equals(S that) const { return identical(that) || expr->equals(*that.expr); }
    bool S::identical(S that) const { return expr == that.expr; }

    S S::operator-() const {
        return unary_op(*this, UnaryOp::Neg);
    }
    S S::operator!() const {
        return unary_op(*this, UnaryOp::Not);
    }
    S S::operator~() const {
        return unary_op(*this, UnaryOp::BitNot);
    }
    S S::ushr(S that) const {
        return binary_op(*this, that, BinaryOp::UnsignedShiftRight);
    }
    S S::abs() const {
        return unary_op(*this, UnaryOp::Abs);
    }
    S S::neg() const {
        return unary_op(*this, UnaryOp::Neg);
    }
    S S::bitNot() const {
        return unary_op(*this, UnaryOp::BitNot);
    }
    S S::inv() const {
        return binary_op(1, *this, BinaryOp::Div);
    }
    S S::clz() const {
        return unary_op(*this, UnaryOp::Clz);
    }
    S S::ctz() const {
        return unary_op(*this, UnaryOp::Ctz);
    }
    S S::clo() const {
        return unary_op(bitNot(), UnaryOp::Clz);
    }
    S S::cto() const {
        return unary_op(bitNot(), UnaryOp::Ctz);
    }
    S S::popcount() const {
        return unary_op(*this, UnaryOp::PopCount);
    }
    S S::numbits() const {
        return unary_op(*this, UnaryOp::NumBits);
    }
    S S::sqrt() const {
        return unary_op(*this, UnaryOp::Sqrt);
    }
    S S::cbrt() const {
        return unary_op(*this, UnaryOp::Cbrt);
    }
    S S::log() const {
        return unary_op(*this, UnaryOp::Log);
    }
    S S::log2() const {
        return unary_op(*this, UnaryOp::Log2);
    }
    S S::log10() const {
        return unary_op(*this, UnaryOp::Log10);
    }
    S S::log1p() const {
        return unary_op(*this, UnaryOp::Log1p);
    }
    S S::exp() const {
        return unary_op(*this, UnaryOp::Exp);
    }
    S S::exp2() const {
        return unary_op(*this, UnaryOp::Exp2);
    }
    S S::exp10() const {
        return unary_op(*this, UnaryOp::Exp10);
    }
    S S::expm1() const {
        return unary_op(*this, UnaryOp::Expm1);
    }
    S S::floor() const {
        return unary_op(*this, UnaryOp::Floor);
    }
    S S::ceil() const {
        return unary_op(*this, UnaryOp::Ceil);
    }
    S S::round() const {
        return unary_op(*this, UnaryOp::Round);
    }
    S S::trunc() const {
        return unary_op(*this, UnaryOp::Trunc);
    }
//    std::pair<S, S> S::sincos() const {
//        return synthdef::sincos(*this);
//    }
    S S::sinpi() const {
        return unary_op(*this, UnaryOp::SinPi);
    }
    S S::cospi() const {
        return unary_op(*this, UnaryOp::CosPi);
    }
    S S::tanpi() const {
        return unary_op(*this, UnaryOp::TanPi);
    }
    S S::sin() const {
        return unary_op(*this, UnaryOp::Sin);
    }
    S S::cos() const {
        return unary_op(*this, UnaryOp::Cos);
    }
    S S::tan() const {
        return unary_op(*this, UnaryOp::Tan);
    }
    S S::asin() const {
        return unary_op(*this, UnaryOp::Asin);
    }
    S S::acos() const {
        return unary_op(*this, UnaryOp::Acos);
    }
    S S::atan() const {
        return unary_op(*this, UnaryOp::Atan);
    }
    S S::sinh() const {
        return unary_op(*this, UnaryOp::Sinh);
    }
    S S::cosh() const {
        return unary_op(*this, UnaryOp::Cosh);
    }
    S S::tanh() const {
        return unary_op(*this, UnaryOp::Tanh);
    }
    S S::asinh() const {
        return unary_op(*this, UnaryOp::Asinh);
    }
    S S::acosh() const {
        return unary_op(*this, UnaryOp::Acosh);
    }
    S S::atanh() const {
        return unary_op(*this, UnaryOp::Atanh);
    }
    S S::erf() const {
        return unary_op(*this, UnaryOp::Erf);
    }
    S S::erfc() const {
        return unary_op(*this, UnaryOp::Erfc);
    }
    
//    S S::matmul(S b) const {
//        auto ca = as<Constant>();
//        auto cb = b.as<Constant>();
//        if (ca && cb) {
//            auto cc = matrix_multiply(ca, cb);
//            return addConstantExpr(cc);
//        } else {
//            return addExpr(new MatMulExpr(*this, b));
//        }
//    }
    
    S S::mod(S that) const {
        return binary_op(*this, that, BinaryOp::Mod);
    }
    S S::min(S that) const {
        return binary_op(*this, that, BinaryOp::Min);
    }
    S S::max(S that) const {
        return binary_op(*this, that, BinaryOp::Max);
    }
    S S::pow(S that) const {
        return binary_op(*this, that, BinaryOp::Pow);
    }
    S S::atan2(S that) const {
        return binary_op(*this, that, BinaryOp::Atan2);
    }
    S S::hypot(S that) const {
        return binary_op(*this, that, BinaryOp::Hypot);
    }
    S S::copysign(S that) const {
        return binary_op(*this, that, BinaryOp::Copysign);
    }
    S S::eq(S that) const {
        return compare_op(*this, that, CompareOp::EQ);
    }
    S S::ne(S that) const {
        return compare_op(*this, that, CompareOp::NE);
    }
    S S::lt(S that) const {
        return compare_op(*this, that, CompareOp::LT);
    }
    S S::le(S that) const {
        return compare_op(*this, that, CompareOp::LE);
    }
    S S::gt(S that) const {
        return compare_op(*this, that, CompareOp::GT);
    }
    S S::ge(S that) const {
        return compare_op(*this, that, CompareOp::GE);
    }
    
    S S::cast_i32() const {
        auto a = as<Constant>();
        if (a) {
            auto b = to_int(a);
            b->init_type = NumType::i32;
            return addConstantExpr(b);
        } else {
            return addExpr(new CastOpExpr{NumType::i32, expr});
        }
    }
    
    S S::cast_i64() const {
        auto a = as<Constant>();
        if (a) {
            auto b = to_int(a);
            b->init_type = NumType::i64;
            return addConstantExpr(b);
        } else {
            return addExpr(new CastOpExpr(NumType::i64, expr));
        }
    }
    S S::cast_f32() const {
        auto a = as<Constant>();
        if (a) {
            auto b = to_float(a);
            b->init_type = NumType::f32;
            return addConstantExpr(b);
        } else {
            return addExpr(new CastOpExpr(NumType::f32, expr));
        }
    }
    S S::cast_f64() const {
        auto a = as<Constant>();
        if (a) {
            auto b = to_float(a);
            b->init_type = NumType::f32;
            return addConstantExpr(b);
        } else {
            return addExpr(new CastOpExpr(NumType::f64, expr));
        }
    }
    
    S S::reduce(BinaryOp op, usize cols) const {
        return synthdef::reduce(*this, op, cols);
    }
    
    D::D() : delayBuf(new DelayBuf()) {
        delayBuf->graph = gGraph;
        gGraph->delayBufs.insert(*this);
        gSynth->delayBufs.insert(*this);
    }
    D::D(S maxDelay) : delayBuf(new DelayBuf(maxDelay))
    {
        delayBuf->graph = gGraph;
        gGraph->delayBufs.insert(*this);
        gSynth->delayBufs.insert(*this);
    }
    D::D(DelayBuf* d) : delayBuf(d) {
    }
    D::D(D const& other) : delayBuf(other.delayBuf) {
    }

    S D::setMaxDelay(S maxDelayArg) {
        S maxDelay = addExpr(new MaxDelay(delayBuf, maxDelayArg));
        delayBuf->maxDelay = maxDelay;
        return maxDelay;
    }
    
    S D::read(usize delaySamples) const {
        return addExpr(new DelayFixRead{delayBuf, delaySamples});
    }
    S D::vread(S delaySamples) const {
        return addExpr(new DelayVarRead{delayBuf, delaySamples});
    }
    S D::operator()(usize delaySamples) const {
        return read(delaySamples);
    }
    S D::operator[](usize delaySamples) const {
        return read(delaySamples);
    }
    S D::v(S delaySamples) const {
        return vread(delaySamples);
    }
    D::operator S() {
        return read(0);
    }
    
    S D::init(usize sampleIndex, S value) {
        return addExpr(new DelayInit{delayBuf, sampleIndex, value});
    }
    S D::write(S value) {
        return addExpr(new DelayWrite{delayBuf, value});
    }
    S D::operator=(S value) {
        write(value);
        return value;
    }
    
    D& D::operator+=(S value) {
        write(read(1) + value); return *this;
    }
    D& D::operator-=(S value) {
        write(read(1) - value); return *this;
    }
    D& D::operator*=(S value) {
        write(read(1) * value); return *this;
    }
    D& D::operator/=(S value) {
        write(read(1) / value); return *this;
    }
    D& D::operator%=(S value) {
        write(read(1) % value); return *this;
    }
    D& D::operator&=(S value) {
        write(read(1) & value); return *this;
    }
    D& D::operator|=(S value) {
        write(read(1) | value); return *this;
    }
    D& D::operator^=(S value) {
        write(read(1) ^ value); return *this;
    }
    D& D::operator<<=(S value) {
        write(read(1) << value); return *this;
    }
    D& D::operator>>=(S value) {
        write(read(1) >> value); return *this;
    }      

    u64 D::hash() const { return hash64(u64(delayBuf)); }

//    Expr* hash_cons(Expr* expr) {
//        auto it = gGraph->hashConsSet.find(expr);
//        if (it != gGraph->hashConsSet.end()) {
//            expr = it->get();
//        } else {
//            gGraph->hashConsSet.insert(expr);
//        }
//        return expr;
//    }

#if 0
    S S::reverse(Axis axis) const {
        auto a = as<Constant>();
        if (a) {
            return addConstantExpr(a->reverse(axis));
        } else {
            return addExpr(new MatReverse{expr, axis});
        }
    }

    S S::take(usize n, Axis axis) const {
        auto a = as<Constant>();
        if (a) {
            return addConstantExpr(a->take(n, axis));
        } else {
            return addExpr(new MatTake{expr, n, axis});
        }

    }

    S S::skip(usize n, Axis axis) const {
        auto a = as<Constant>();
        if (a) {
            return addConstantExpr(a->skip(n, axis));
        } else {
            return addExpr(new MatSkip{expr, n, axis});
        }
    }
    S S::stride(usize n, Axis axis) const {
        auto a = as<Constant>();
        if (a) {
            return addConstantExpr(a->stride(n, axis));
        } else {
            return addExpr(new MatStride{expr, n, axis});
        }
    }
    S S::stutter(usize n, Axis axis) const {
        auto a = as<Constant>();
        if (a) {
            return addConstantExpr(a->stutter(n, axis));
        } else {
            return addExpr(new MatStutter{expr, n, axis});
        }
    }
    S S::cyc(usize n, Axis axis) const {
        auto a = as<Constant>();
        if (a) {
            return addConstantExpr(a->cyc(n, axis));
        } else {
            return addExpr(new MatCyc{expr, n, axis});
        }
    }
    S S::rotate(S n, Axis axis) const {
        Constant const* ca = as<Constant>();
        Constant const* cn = n.as<Constant>();
        if (ca && cn && cn->is_scalar()) { 
            isize nn = isize(cn->get_scalar_int().value());
            return addConstantExpr(ca->rotate(nn, axis)); 
        } 
        return addExpr(new MatRotate{expr, n, axis});
    }
    S S::transpose() const {
        auto a = as<Constant>();
        if (a) {
            return addConstantExpr(a->transpose());
        } else {
            return addExpr(new MatTranspose{expr});
        }
    }
    S S::at(S i) const {
        auto ca = as<Constant>();
        auto ci = i.as<Constant>();
        if (ca && ci) {
            return addConstantExpr(ca->at(ci));
        } else {
            return addExpr(new MatAt{expr, i});
        }
    }
    S S::put(S i, S values) const {
        auto ca = as<Constant>();
        auto ci = i.as<Constant>();
        auto cv = values.as<Constant>();
        if (ca && ci && cv) {
            return addConstantExpr(ca->put(ci, cv));
        } else {
            return addExpr(new MatPut{expr, i, values});
        }
    }
#endif

} // namespace synthdef
