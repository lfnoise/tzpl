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
//  synthdef_builtin_ops.cpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 3/5/24.
//

#include "synthdef_builtin_ops.hpp"
#include "synthdef_synth.hpp"
#include "tzpl_random.hpp"
#include "tzpl_plugin_abi.h"
#include "synthdef_matrix.hpp"

namespace synthdef {

    S z1(S a) {
        D d;
        d.write(a);
        return d(1);
    }
    
    S z2(S a){
        D d;
        d.write(a);
        return d(2);
    } 


    // Expr creation functions.

    S constant(usize chans, i64 value) {
        chans = asChans(chans);
        return addConstantExpr(new Constant(vector<i64>(chans, value)));
    }
    S constant(usize chans, f64 value) {
        chans = asChans(chans);
        return addConstantExpr(new Constant(vector<f64>(chans, value)));
    }
    S constant(usize chans, vector<i64> values) {
        return addConstantExpr(new Constant(std::move(values)));
    }
    S constant(usize chans, vector<f64> values) {
        return addConstantExpr(new Constant(std::move(values)));
    }

    S constant(i64 value) {
        return addConstantExpr(new Constant(vector<i64>{value}));
    }
    S constant(f64 value) {
        return addConstantExpr(new Constant(vector<f64>{value}));
    }
    S constant(vector<i64> values) {
        return addConstantExpr(new Constant(std::move(values)));
    }
    S constant(vector<f64> values) {
        return addConstantExpr(new Constant(std::move(values)));
    }

    template <typename T>
    S iota(usize n, T first, T step) {
        return addConstantExpr(new Constant(VectorT<T>::iota(n, first, step)));
    }
    template S iota<i64>(usize, i64, i64);
    template S iota<f64>(usize, f64, f64);
    
    template <typename T>
    S geo(usize n, T first, T grow) {
        return addConstantExpr(new Constant(VectorT<T>::geo(n, first, grow)));
    }
    template S geo<i64>(usize, i64, i64);
    template S geo<f64>(usize, f64, f64);

    template <typename T>
    S linspace(usize n, T first, T last) {
        return addConstantExpr(new Constant(VectorT<T>::linspace(n, first, last)));
    }
    template S linspace<i64>(usize, i64, i64);
    template S linspace<f64>(usize, f64, f64);

    S logspace(usize n, f64 first, f64 last) {
        return addConstantExpr(addConstantExpr(new Constant(VectorT<f64>::logspace(n, first, last))));
    }
    
    S fs() {
        return addExpr(new SampleRate());
    }
    S sd() {
        return addExpr(new SampleDur());
    }
    S control(string name, ControlSpec const& spec, NumType type, usize chans) {
        return addExpr(new Control{spec, type, chans, name});
    }
    S inlet(NumType type, usize chans, string name) {
        return addExpr(new Inlet{type, chans, name});
    }
    S outlet(S value, string name) {
        return addExpr(new Outlet{value, name});
    }
    
    S urand(usize chans, SignalRate rate) {
        if (rate == eventSignalRate) {
            throw std::runtime_error("urand: event rate not supported.");
        } else if (rate == constSignalRate) {
            vector<f64> v;
            RandState1 r;
            arc4seedrand(r);
            for (usize i = 0; i < chans; ++i) {
                v[i] = urand(r);
            }
            return addConstantExpr(new Constant(v));
        }
        return addExpr(new URandExpr{chans, rate});
    }
    S birand(usize chans, SignalRate rate) {
        if (rate == eventSignalRate) {
            throw std::runtime_error("birand: event rate not supported.");
        } else if (rate == constSignalRate) {
            vector<f64> v;
            RandState1 r;
            arc4seedrand(r);
            for (usize i = 0; i < chans; ++i) {
                v[i] = birand(r);
            }
            return addConstantExpr(new Constant(v));
        }
        return addExpr(new BiRandExpr{chans, rate});
    }
    S rand64(usize chans, SignalRate rate) {
        if (rate == eventSignalRate) {
            throw std::runtime_error("rand64: event rate not supported.");
        } else if (rate == constSignalRate) {
            vector<f64> v;
            RandState1 r;
            arc4seedrand(r);
            for (usize i = 0; i < chans; ++i) {
                v[i] = rand64(r);
            }
            return addConstantExpr(new Constant(v));
        }
        return addExpr(new Rand64Expr{chans, rate});
    }

    S sharedIn(usize slot, SignalRate rate) {
        if (slot >= TZPL_SHARED_INPUT_SLOTS) {
            throw std::runtime_error("sharedIn: slot out of range.");
        }
        if (rate != initSignalRate && rate != audioSignalRate) {
            throw std::runtime_error("sharedIn: only init and audio rate supported.");
        }
        return addExpr(new SharedInExpr{slot, rate});
    }

    // Promote an event-rate signal to audio rate (a stepped audio signal
    // re-read every sample). No effect on constant/init/reset-rate signals
    // -- and on audio-rate ones -- which pass through unchanged.
    S eventToAudio(S a) {
        if (a->rate != eventSignalRate) {
            return a;
        }
        return addExpr(new EventToAudioExpr{a});
    }

    // Expr math functions.
    S abs(S a) { return a.abs(); }
    S neg(S a) { return a.neg(); }
    S bitNot(S a) { return a.bitNot(); }
    S inv(S a) { return a.inv(); } // re
    S clz(S a) { return a.clz(); } // count leading zeroes
    S ctz(S a) { return a.ctz(); } // count trailing zeroes
    S clo(S a) { return a.clo(); } // count leading ones
    S cto(S a) { return a.cto(); } // count trailing ones
    S popcount(S a) { return a.popcount(); }
    S numbits(S a) { return a.numbits(); }
    S sqrt(S a) { return a.sqrt(); }
    S cbrt(S a) { return a.cbrt(); }
    S floor(S a) { return a.floor(); }
    S ceil(S a) { return a.ceil(); }
    S round(S a) { return a.round(); }
    S trunc(S a) { return a.trunc(); }
    S log(S a) { return a.log(); }
    S log2(S a) { return a.log2(); }
    S log10(S a) { return a.log10(); }
    S log1p(S a) { return a.log1p(); }
    S exp(S a) { return a.exp(); }
    S exp2(S a) { return a.exp2(); }
    S exp10(S a) { return a.exp10(); }
    S expm1(S a) { return a.expm1(); }
    S sinpi(S a) { return a.sinpi(); }
    S cospi(S a) { return a.cospi(); }
    S tanpi(S a) { return a.tanpi(); }
    S sin(S a) { return a.sin(); }
    S cos(S a) { return a.cos(); }
    S tan(S a) { return a.tan(); }
    S asin(S a) { return a.asin(); }
    S acos(S a) { return a.acos(); }
    S atan(S a) { return a.atan(); }
    S sinh(S a) { return a.sinh(); }
    S cosh(S a) { return a.cosh(); }
    S tanh(S a) { return a.tanh(); }
    S asinh(S a) { return a.asinh(); }
    S acosh(S a) { return a.acosh(); }
    S atanh(S a) { return a.atanh(); }
    S erf(S a) { return a.erf(); }
    S erfc(S a) { return a.erfc(); }

//    S matmul(S a, S b) { return a.matmul(b); }

    S mod(S a, S b) { return a.mod(b); }
    S min(S a, S b) { return a.min(b); }
    S max(S a, S b) { return a.max(b); }
    S pow(S a, S b) { return a.pow(b); }
    S atan2(S a, S b) { return a.atan2(b); }
    S hypot(S a, S b) { return a.hypot(b); }
    S copysign(S a, S b) { return a.copysign(b); }

    S operator+(S a, S b) {
        return binary_op(a, b, BinaryOp::Add);
    }
    S operator-(S a, S b) {
        return binary_op(a, b, BinaryOp::Sub);
    }
    S operator*(S a, S b) {
        return binary_op(a, b, BinaryOp::Mul);
    }
    S operator/(S a, S b) {
        return binary_op(a, b, BinaryOp::Div);
    }
    S operator%(S a, S b) {
        return binary_op(a, b, BinaryOp::Rem);
    }
    S operator&(S a, S b) {
        return binary_op(a, b, BinaryOp::BitAnd);
    }
    S operator|(S a, S b) {
        return binary_op(a, b, BinaryOp::BitOr);
    }
    S operator^(S a, S b) {
        return binary_op(a, b, BinaryOp::BitXor);
    }
    S operator<<(S a, S b) {
        return binary_op(a, b, BinaryOp::ShiftLeft);
    }
    S operator>>(S a, S b) {
        return binary_op(a, b, BinaryOp::ShiftRight);
    }
    
    S& operator+=(S& a, S const& b) {
        return a = a + b;
    }
    S& operator-=(S& a, S const& b) {
        return a = a - b;
    }
    S& operator*=(S& a, S const& b) {
        return a = a * b;
    }
    S& operator/=(S& a, S const& b) {
        return a = a / b;
    }
    S& operator%=(S& a, S const& b) {
        return a = a % b;
    }
    S& operator&=(S& a, S const& b) {
        return a = a & b;
    }
    S& operator|=(S& a, S const& b) {
        return a = a | b;
    }
    S& operator^=(S& a, S const& b) {
        return a = a ^ b;
    }
    S& operator<<=(S& a, S const& b) {
        return a = a << b;
    }
    S& operator>>=(S& a, S const& b) {
        return a = a >> b;
    }
    
    S eq(S a, S b) { return a.eq(b); }
    S ne(S a, S b) { return a.ne(b); }
    S lt(S a, S b) { return a.lt(b); }
    S le(S a, S b) { return a.le(b); }
    S gt(S a, S b) { return a.gt(b); }
    S ge(S a, S b) { return a.ge(b); }
    
    S cast_i32(S a) {
        return a.cast_i32();
    }
    S cast_i64(S a) {
        return a.cast_i64();
    }
    S cast_f32(S a) {
        return a.cast_f32();
    }
    S cast_f64(S a) {
        return a.cast_f64();
    }
    
    S operator==(S a, S b) { return a.eq(b); }
    S operator!=(S a, S b) { return a.ne(b); }
    S operator<(S a, S b) { return a.lt(b); }
    S operator<=(S a, S b) { return a.le(b); }
    S operator>(S a, S b) { return a.gt(b); }
    S operator>=(S a, S b) { return a.ge(b); }

    S for_(S count, std::function<S(S)> body) {
        Constant const* ca = count.as<Constant>();
        if (ca && ca->is_scalar()) {
            i64 n = ca->get_scalar_int().value();
            if (n <= 0) { return S(0); }
            if (n == 1) { return body(S(0)); }
        }
        S b = newSubgraph(body);
        return addExpr(new ForLoopExpr(count, b));
    }
    
    S if_(S test, std::function<S()> thenClause) {
        std::println("if_");
        Constant const* ca = test.as<Constant>();
        if (ca && ca->is_scalar()) {
            i64 cond = ca->get_scalar_int().value();
            if (cond) {
                return newSubgraph(thenClause);
            } else {
                return S(0);
            }
        }
        S a = newSubgraph(thenClause);
        S b = newSubgraph([](){ return S(0); });
        return addExpr(new IfElseExpr(test, a, b));
    }
    S if_(S test, std::function<S()> thenClause, std::function<S()> elseClause) {
        Constant const* ca = test.as<Constant>();
        if (ca && ca->is_scalar()) {
            i64 cond = ca->get_scalar_int().value();
            if (cond) {
                return newSubgraph(thenClause);
            } else {
                return newSubgraph(elseClause);
            }
        }
        S a = newSubgraph(thenClause);
        S b = newSubgraph(elseClause);
        return addExpr(new IfElseExpr(test, a, b));
    }
    
    S switch_(S test, std::vector<std::function<S()>> inputs) {
        assert(inputs.size() >= 3);
        Constant const* ca = test.as<Constant>();
        if (ca && ca->is_scalar()) {
            usize selector = usize(ca->get_scalar_int().value());
            selector = std::clamp(selector, usize(0), inputs.size()-1);
            return newSubgraph(inputs[selector]);
        }
        std::vector<S> vin = inputs | stdv::transform([](auto& f){ return newSubgraph(f); }) | stdr::to<std::vector>();
        return addExpr(new SwitchExpr(test, vin));
    }
            
    S sel(vector<S> inputs) {
        assert(inputs.size() >= 3);
        Constant const* ca = inputs[0].as<Constant>();
        if (ca && ca->is_scalar()) {
            usize selector = usize(ca->get_scalar_int().value());
            selector = 1 + std::clamp(selector, usize(0), inputs.size()-2);
            return inputs[selector];
        }
        return addExpr(new SelectExpr{inputs});
    }
    S sel2(S a, S ifTrue, S ifFalse) {
        Constant const* ca = a.as<Constant>();
        if (ca && ca->is_scalar()) {
            i64 cond = ca->get_scalar_int().value();
            return cond ? ifTrue : ifFalse;
        }
        return sel({a, ifFalse, ifTrue});
    }

#if 0
    S take(S a, usize n, usize rows) {
        return a.take(n, rows);
    }
    S skip(S a, usize n, usize rows) {
        return a.skip(n, rows);
    }
    S stride(S a, usize n, usize rows) {
        return a.stride(n, rows);
    }
    S stutter(S a, usize n, usize rows) {
        return a.stutter(n, rows);
    }
    S cyc(S a, usize n, usize rows) {
        return a.cyc(n, rows);
    }
    S reverse(S a, usize rows) {
        return a.reverse(rows);
    }
    S rotate(S a, S n, usize rows) {
        return a.rotate(n, rows);
    }
    S permute(S a, S i, usize rows) {
        Constant const* ca = a.as<Constant>();
        Constant const* ci = i.as<Constant>();
        if (ca && ci) { 
            return addConstantExpr(ca->permute(ci, rows)); 
        } 
        return addExpr(new VecPermute{a, i, rows});
    }
    S transpose(S a) {
        return a.transpose();
    }

    S lace(vector<S> inputs, usize rows) {
        bool all_constant = true;
        for (S in : inputs) {
            if (!in->is_constant()) {
                all_constant = false;
                break;
            }
        }
            
        if (all_constant) {
            bool is_f64 = false;
            Shape new_shape;
            for (S in : inputs) {
                Constant const* k = in.as<Constant>();
                try {
                    new_shape = new_shape.broadcast(k->shape);
                } catch (std::runtime_error const& e) {
                    throw std::runtime_error(std::format("lace: incompatible shapes, {}", e.what()));
                }
                if (k->is_f64()) { is_f64 = true; }
            }
            

            if (is_f64) {
                vector<f64> new_values;
// FIXME
//                for (usize i = 0; i < len; ++i) {
//                    for (S in : inputs) {
//                        Constant const* k = in.as<Constant>();
//                        f64 x = k->f64_at(mod(i, k->size()));
//                        new_values.push_back(x);
//                    }
//                }
                return addConstantExpr(new Constant(new_values));
            } else {
                vector<i64> new_values;
// FIXME
//                for (usize i = 0; i < len; ++i) {
//                    for (S in : inputs) {
//                        Constant const* k = in.as<Constant>();
//                        i64 x = k->i64_at(mod(i, k->size()));
//                        new_values.push_back(x);
//                    }
//                }
                return addConstantExpr(new Constant(new_values));
            }
        } else {
            SignalRate rate = constSignalRate;
            for (S in : inputs) {
                rate = rate.max(in->rate);
            }
            return addExpr(new VecLace{rate, rows, inputs});
        }
    }

    S cat(vector<S> inputs, usize rows) {
        assert(inputs.size() > 0);
        bool all_constant = true;
        for (S in : inputs) {
            if (!in->is_constant()) {
                all_constant = false;
                break;
            }
        }
            
        if (all_constant) {
            Constant* out = inputs[0].as<Constant>();
            for (S in : inputs | stdv::drop(1)) {
                Constant const* k = in.as<Constant>();
                out = out->cat(k, rows);
            }
            return addConstantExpr(out);
        } else {
            return addExpr(new VecCat{maxRate(inputs), rows, inputs});
        }
    }
#endif

    S spectral_chain(S input, int fftSize, int hopSize, std::function<S(S)> body) {
        assert(fftSize > 0 && (fftSize & (fftSize - 1)) == 0); // must be power of 2
        assert(hopSize > 0 && hopSize <= fftSize);
        S out;
        SpectralFrameInput* framePtr;
        {
            Graph* graph = new Graph(gSynth, gGraph);
            PushGraph pg(graph);
            S frame = addExpr(new SpectralFrameInput(fftSize));
            framePtr = frame.as<SpectralFrameInput>();
            // Set the shape: inputChans * fftSize
            frame->chans = input->chans * fftSize;
            out = addExpr(new PhiNodeExpr(body(frame)));
        }
        // SpectralChainExpr is added in the parent graph (after PushGraph is destroyed)
        S result = addExpr(new SpectralChainExpr(input, fftSize, hopSize, out));
        // Set back-pointer so SpectralFrameInput codegen can find its owning chain
        framePtr->chainSerial = result->userial;
        return result;
    }

    S newSubgraph(std::function<S()> f) {
        Graph* graph = new Graph(gSynth, gGraph);
        PushGraph pg(graph);
        S out = addExpr(new PhiNodeExpr(f()));
        std::println("newGraph out {} {}", out->typeName(), out->chans);
        return out;
    }

    S newSubgraph(std::function<S(S)> f) {
        Graph* graph = new Graph(gSynth, gGraph);
        PushGraph pg(graph);
        S arg = addExpr(new VarExpr("i", NumType::any_int));
        S out = addExpr(new PhiNodeExpr(f(arg)));
        std::println("newGraph out {} {}", out->typeName(), out->chans);
        return out;
    }
}
