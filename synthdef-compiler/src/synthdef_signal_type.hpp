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
//  synthdef_signal_type.hpp
//  synthdef-compiler
//
//  Created by James McCartney on 2/17/25.
//

#ifndef synthdef_signal_type_hpp
#define synthdef_signal_type_hpp

#include "synthdef_types2.hpp"
#include <bit>

namespace synthdef {

    struct SignalRate {
        enum RateEnum {
            Const,
            Init,
            Reset,
            Event,
            Audio,
        };

        RateEnum rate;

        SignalRate(RateEnum rate) : rate(rate) {}
        bool operator==(SignalRate that) const { return rate == that.rate; }  
        bool operator!=(SignalRate that) const { return rate != that.rate; }
        bool operator<(SignalRate that) const { return rate < that.rate; }

        SignalRate max(SignalRate that) const { return SignalRate{std::max(rate, that.rate)}; }

        u64 hash() const;
        
        string str() const {
            switch (rate) {
                case Const: return "Const";
                case Init: return "Init";
                case Reset: return "Reset";
                case Event: return "Event";
                case Audio: return "Audio";
                default: return "Unknown";
            }
        }
        string codeStr() const {
            switch (rate) {
                case Const: return "tzpl_constRate";
                case Init: return  "tzpl_initRate";
                case Reset: return "tzpl_resetRate";
                case Event: return "tzpl_eventRate";
                case Audio: return "tzpl_audioRate";
                default: return "Unknown";
            }
        }
    };

    static const SignalRate constSignalRate = SignalRate::Const;
    static const SignalRate initSignalRate  = SignalRate::Init;
    static const SignalRate resetSignalRate = SignalRate::Reset;
    static const SignalRate eventSignalRate = SignalRate::Event;
    static const SignalRate audioSignalRate = SignalRate::Audio;

    enum NumTypeFlags : u8 {
        I32 = 1,
        I64 = 2,
        F32 = 4,
        F64 = 8,
    };

    struct NumType {
        u8 flags;

        NumType() : flags(I32 | I64 | F32 | F64) {}
        NumType(u8 flags) : flags(flags) {}

        u64 hash() const;

        static const NumType empty;
        static const NumType any;
        static const NumType any_int;
        static const NumType any_float;
        static const NumType i32;
        static const NumType i64;
        static const NumType f32;
        static const NumType f64;
        
        operator bool() const { return flags != 0; }

        bool is_empty() const { return flags == 0; }
        bool is_i32() const { return (flags & I32) == I32; }
        bool is_i64() const { return (flags & I64) == I64; }
        bool is_f32() const { return (flags & F32) == F32; }
        bool is_f64() const { return (flags & F64) == F64; }

        bool supports_int() const { return (flags & (I32 | I64)); }
        bool supports_float() const { return (flags & (F32 | F64)); }
        bool supports_32_bits() const { return (flags & (I32 | F32)); }
        bool supports_64_bits() const { return (flags & (I64 | F64)); }

        bool is_32_bits() const { return supports_32_bits() && !supports_64_bits(); }
        bool is_64_bits() const { return supports_64_bits() && !supports_32_bits(); }
        bool is_float() const { return supports_float() && !supports_int(); }
        bool is_int() const { return supports_int() && !supports_float(); }

        bool is_concrete() const { return std::popcount(flags) == 1; }

        NumType operator&(NumType that) const { 
            u8 new_flags = flags & that.flags;
            return NumType(new_flags); 
        }
        NumType operator|(NumType that) const { return NumType(flags | that.flags); }

        NumType as_bool_type() const {
            u8 new_flags = 0;
            if (supports_32_bits()) {
                new_flags |= I32;
            }
            if (supports_64_bits()) {
                new_flags |= I64;
            }   
            return NumType(new_flags);
        }

        bool operator==(NumType that) const { return flags == that.flags; }
        bool operator!=(NumType that) const { return flags != that.flags; }
        
        string str() const {
            switch (flags) {
                case 0: return "empty";
                case I32: return "i32";
                case I64: return "i64";
                case F32: return "f32";
                case F64: return "f64";
                case I32 | I64 | F32 | F64: return "any";
                case I32 | I64: return "any_int";
                case F32 | F64: return "any_float";
                case I32 | F32: return "any_32_bits";
                case I64 | F64: return "any_64_bits";
                default: {
                    string result = "{";
                    if (flags & I32) {
                        result += "i32, ";
                    }
                    if (flags & I64) {
                        result += "i64, ";
                    }
                    if (flags & F32) {
                        result += "f32, ";
                    }
                    if (flags & F64) {
                        result += "f64, ";
                    }
                    result.pop_back();
                    result.pop_back();
                    result += "}";
                    return result;
                }
            }
        }
    };

    enum class ShapeKind {
        Scalar,
        Vector,
    };

    inline usize asChans(usize x) {
        return std::bit_ceil(std::max(usize(1), x));
    }

    inline usize broadcast(usize a, usize b) {
        return std::max(a, b);
    }
    
    

    struct ControlSpec {
        f64 lo, hi, param;      // param holds the init value
        int warp;               // lang ControlWarp ordinal (ABI ordinal - 1)
        f64 warpParam = 0.0;    // warp payload (step size for step warp)
        int kind = 0;           // tzpl_ControlKind ordinal (0 = continuous)

        u64 hash() const;
        bool operator==(ControlSpec const& that) const {
            return lo == that.lo
                && hi == that.hi
                && param == that.param
                && warp == that.warp
                && warpParam == that.warpParam
                && kind == that.kind;
        }
    };

}

#endif /* synthdef_signal_type_hpp */
