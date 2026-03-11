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
//  tzpl_plugin_interface.hpp
//  audio engine
//
//  Created by James McCartney on 2/2/21.
//

#ifndef tzpl_plugin_interface_h
#define tzpl_plugin_interface_h

#include "tzpl_random.hpp"
#include <bit>

//namespace engine {
//
//struct Engine;
//
//struct Node;
//struct tzpl_SynthData;
//
//enum tzpl_SErr {
//    tzpl_errNone,
//    errInternal,
//    errNodeIDAlreadyTaken,
//    errNodeDefNotFound,
//    errNodeNotFound,
//    errNoteNotFound,
//    errControlNotFound,
//    errDeviceNotFound,
//    errAlreadyAdded,
//    errAlreadyRemoved,
//    errSiloOutOfRange,
//    errInputOutOfRange,
//    errOutputOutOfRange,
//    errNoAudioDevices,
//    errAudioNotInitialized,
//    errCommandsQueuedButNotSent,
//    errNoActiveBundle,
//    errEngineInUse,
////    errCyclicConnection,
//    errNodeIsNull,
//
//    errTypeMismatch,
//    errRateMismatch,
//    errChanMismatch,
//    errNumPortsMismatch,
//
//    errNotImplemented,
//    
//    errTooLate,
//};
//
//// signal rate
//enum Rate {
//    constRate,
//    initRate,
//    resetRate,
//    pushRate,
//    audioRate
//};
//
//// signal type
//enum ElemType {
//    kI32 = 0,
//    kF32 = 1,
//
//    kI64 = 2,
//    kF64 = 3,
//};
//
//struct tzpl_SignalType {
//    ElemType elem; 
//    Rate rate; 
//    int chans; // number of channels.
//};
//
//struct tzpl_ParamPair {
//	int index;
//	f32 value;
//};
//
//inline bool isFloat(ElemType e) {
//    switch (e) {
//        case kF32 :
//        case kF64 :
//            return true;
//        default:
//            return false;
//    }
//}
//
//inline int elemSize(ElemType e) {
//    switch (e) {
//        case kI32 :
//            return sizeof(i32);
//        case kF32 :
//            return sizeof(f32);
//        case kI64 :
//            return sizeof(i64);
//        case kF64 :
//            return sizeof(f64);
//    }
//}
//
//
//struct SynthFuns {
//    tzpl_SynthData* (*alloc)();
//    tzpl_SErr (*free)(tzpl_SynthData* synth);
//    tzpl_SErr (*init)(tzpl_SynthData* synth);
//    tzpl_SErr (*uninit)(tzpl_SynthData* synth); // optional
//    tzpl_SErr (*reset)(tzpl_SynthData* synth); // optional
//    tzpl_SErr (*event)(tzpl_SynthData* synth, u64 id, tzpl_Slice dst, tzpl_Slice data);
//    //tzpl_SErr (*push)(tzpl_SynthData* synth, int inputIndex, int numChannels, void* data); // optional
//    void (*processEvents)(tzpl_SynthData* synth);
//    void (*processAudio)(tzpl_SynthData* synth);
//    
//    // note allocation. all optional.
//    tzpl_SErr (*noteOn)(tzpl_SynthData* synth, i64 now, int noteID, int n, f32* params);
//    tzpl_SErr (*noteOff)(tzpl_SynthData* synth, i64 now, int noteID);
//    tzpl_SErr (*allNotesOff)(tzpl_SynthData* synth, i64 now);
//    tzpl_SErr (*noteSetParams)(tzpl_SynthData* synth, int noteID, int n, tzpl_ParamPair* params);
//    tzpl_SErr (*noteSetParamRange)(tzpl_SynthData* synth, int noteID, int first, int length, f32* values);
//};
//
//struct PortDef {
//    const char* name;
//    tzpl_SignalType type;
//};
//
//enum ControlKind {
//    ckContinuous, ckTrigger, ckBoolean, ckSelect
//};
//
//enum ControlWarp {
//    cwNone, cwLinear, cwExponential
//};
//
//struct ControlSpecC {
//    f64 lo, hi, init, param;
//    ControlWarp warp;
//    ControlKind kind;
//};
//
//struct ControlDef {
//    const char* name;
//    tzpl_SignalType type;
//    u64 id;
//    ControlSpecC spec;
//};
//
//struct ControlDispatchTable;
//struct ControlDispatchTableEntry {
//    enum {
//        kControlTable,
//        kControlDef,
//    };
//    int which;
//    union {
//        ControlDispatchTable* table;
//        ControlDef* def;
//    };
//};
//
//struct ControlDispatchTable {
//    usize size;
//    const char** names;
//    ControlDispatchTableEntry* entries;
//};
//
//
//struct SynthDef {
//    const char* name;
//    SynthFuns funs;
//    int num_ins;
//    int num_outs;
//    int num_controls;
//    PortDef* ins;
//    PortDef* outs;
//    ControlDef* controls;
//    ControlDispatchTable controlDispatch;
//};
////struct NodeDefInfo
////{
////	const char* name;
////	SynthFuns funs;
////    int num_ins;
////    int num_outs;
////    int num_controls;
////    PortInfo* ins;
////    PortInfo* outs;
////    ControlInfo* controls;
////};
//
//
//struct tzpl_SynthData {
//    SynthFuns funs;
//    Engine* engine; // Opaque pointer to the real time audio engine.
//    Node* node;
//    int num_ins;
//    int num_outs;
//    int num_controls;
//    void** inlets;
//    void** outlets;
//    void** controls;
//    f64 fs, sd;        // the sampling rate, sample duration.
//    // custom data ...
//};
//
//template <typename T>
//inline T* getIn(tzpl_SynthData* synth, int inletIndex) {
//    return (T*)synth->inlets[inletIndex];
//}
//
//template <typename T>
//inline T* getOut(tzpl_SynthData* synth, int outletIndex) {
//    return (T*)synth->outlets[outletIndex];
//}
//
//
//const int kMaxRank = 8;
//
//tzpl_SErr compatibleTypes(tzpl_SignalType& a, tzpl_SignalType& b);
//tzpl_SErr relaxedCompatibleTypes(tzpl_SignalType& a, tzpl_SignalType& b);
//
//struct ControlSpec {
//    f64 lo, hi, init, param;
//    u32 warp;
//};
//
//
//using LoadNodeDefFun = void (*)(Engine* e);
//
//// inside the load function, fill out the NodeDefInfo and call addNodeDef.
//void addNodeDef(Engine* e, const char* nodeDefString);
//
//void* getInput(tzpl_SynthData* synth, int inputIndex);
//
//f32 debugSignal(const char* label, f32 in, int index);
//f64 debugSignal(const char* label, f64 in, int index);
//i32 debugSignal(const char* label, i32 in, int index);
//i64 debugSignal(const char* label, i64 in, int index);
//
//inline int calcByteSize(tzpl_SignalType const& t) {
//    return t.chans * elemSize(t.elem);
//}
//
//template <typename T, typename D>
//T lerp(D x, T y0, T y1) {
//    static_assert(simd::get_traits<T>::type::count == simd::get_traits<D>::type::count
//               || simd::get_traits<D>::type::count == 1);
//    return y0 + x * (y1 - y0);
//}
//
//inline f32 lerp(f32 x, f32x2 y) {
//    f32x2 xs = {1.f - x, x};
//    return simd::dot(xs, y);
//}
//
//inline f64 lerp(f64 x, f64x2 y) {
//    f64x2 xs = {1. - x, x};
//    return simd::dot(xs, y);
//}
//
//template <typename T, typename D>
//inline T cubicInterpolate(D x, T y0, T y1, T y2, T y3)
//{
//    static_assert(simd::get_traits<T>::type::count == simd::get_traits<D>::type::count
//               || simd::get_traits<D>::type::count == 1);
//    // 4-point, 3rd-order Hermite (x-form)
//    T c0 = y1;
//    T c1 = T(0.5) * (y2 - y0);
//    T c2 = y0 - T(2.5) * y1 + T(2.) * y2 - T(0.5) * y3;
//    T c3 = T(1.5) * (y1 - y2) + T(0.5) * (y3 - y0);
//    
//    return ((c3 * x + c2) * x + c1) * x + c0;
//}
//
//inline f32 cubicInterpolate(f32 x, f32x4 y)
//{
//    // 4-point, 3rd-order Hermite (x-form)
//    f32 x2 = x*x;
//    f32 x3 = x2*x;
//    f32x4 xs = f32x4{1.f, x, x2, x3 };
//    f32x4 c;
//    c.s0 = simd::dot(xs, f32x4{0.,-.5,1.,-.5});
//    c.s1 = simd::dot(xs, f32x4{1.,0.,-2.5,1.5});
//    c.s2 = simd::dot(xs, f32x4{0.,.5,2.,-1.5});
//    c.s3 = simd::dot(xs, f32x4{0.,0.,-.5,.5});
//    
//    return simd::dot(c,y);
//}
//
//inline f64 cubicInterpolate(f64 x, f64x4 y)
//{
//    // 4-point, 3rd-order Hermite
//    f64 x2 = x*x;
//    f64 x3 = x2*x;
//    f64x4 xs = f64x4{1.f, x, x2, x3 };
//    f64x4 c;
//    c.s0 = simd::dot(xs, f64x4{0.,-.5,1.,-.5});
//    c.s1 = simd::dot(xs, f64x4{1.,0.,-2.5,1.5});
//    c.s2 = simd::dot(xs, f64x4{0.,.5,2.,-1.5});
//    c.s3 = simd::dot(xs, f64x4{0.,0.,-.5,.5});
//    
//    return simd::dot(c,y);
//}
//
//
//struct NoInterpolation {
//    static constexpr usize Margin = 0;
//};
//
//struct LinearInterpolation {
//    static constexpr usize Margin = 1;
//};
//
//struct CubicInterpolation {
//    static constexpr usize Margin = 2;
//};
//
//template <class ScalarType, usize Channels, class Interpolation = NoInterpolation>
//struct DelayBuf {
//    static_assert(Channels > 0);
//    static constexpr usize SimdWidth = Channels <= 2 ? Channels
//                                       : Channels <= 4 ? 4
//                                       : 8;
//    static constexpr usize NumBufs = Channels / SimdWidth;
//    
//    using BufType = typename simd::Vector<ScalarType, SimdWidth>::type;
//
//    BufType* buf[NumBufs];
//    u64 mask;
//    u64 wrpos;
//    
//    DelayBuf() = default;
//    
//    void init(usize maxDelaySamples) {
//        usize numSampleFrames = std::bit_ceil(maxDelaySamples + Interpolation::Margin);
//        mask = numSampleFrames - 1;
//        wrpos = 0;
//        for (usize i = 0; i < NumBufs; ++i) {
//            buf[i] = (BufType*)calloc(1, numSampleFrames * sizeof(BufType));
//        }
//    }
//    void init(f64 maxDelaySamples) {
//        init(usize(ceil(maxDelaySamples)));
//    }
//    
//    void reset() {
//        wrpos = 0;
//    }
//    
//    void step() {
//        ++wrpos;
//    }
//    
//    void write(BufType x, int bufIndex = 0) {
//        buf[bufIndex][wrpos & mask] = x;
//    }
//    void writeAt(BufType x, i64 delayIndex, int bufIndex = 0) {
//        buf[bufIndex][(wrpos - delayIndex) & mask] = x;
//    }
//
//    // 1-channel delay time. SimdWidth can be 1, 2, 4, 8.
//    BufType read(i64 delayIndex, int bufIndex = 0) {
//        auto sampleIndex = (wrpos - delayIndex) & mask;
//        return buf[bufIndex][sampleIndex];
//    }
//
//    // 2-channel delay time. SimdWidth must be 2.
//    BufType read(i64x2 delayIndex, int bufIndex = 0) {
//        static_assert(SimdWidth == 2);
//        i64x2 sampleIndex = (wrpos - delayIndex) & mask;
//        BufType out;
//        BufType* a = buf[bufIndex];
//        out.s0 = a[sampleIndex.s0].s0;
//        out.s1 = a[sampleIndex.s1].s1;
//        return out;
//    }
//
//    // 4-channel delay time. SimdWidth must be 4.
//    BufType read(i64x4 delayIndex, int bufIndex = 0) {
//        static_assert(SimdWidth == 4);
//        i64x4 sampleIndex = (wrpos - delayIndex) & mask;
//        BufType out;
//        BufType* a = buf[bufIndex];
//        out.s0 = a[sampleIndex.s0].s0;
//        out.s1 = a[sampleIndex.s1].s1;
//        out.s2 = a[sampleIndex.s2].s2;
//        out.s3 = a[sampleIndex.s3].s3;
//        return out;
//    }
//
//    // 8-channel delay time. SimdWidth must be 8.
//    BufType read(i64x8 delayIndex, int bufIndex = 0) {
//        static_assert(SimdWidth == 8);
//        i64x8 sampleIndex = (wrpos - delayIndex) & mask;
//        BufType out;
//        BufType* a = buf[bufIndex];
//        out.s0 = a[sampleIndex.s0].s0;
//        out.s1 = a[sampleIndex.s1].s1;
//        out.s2 = a[sampleIndex.s2].s2;
//        out.s3 = a[sampleIndex.s3].s3;
//        out.s4 = a[sampleIndex.s4].s4;
//        out.s5 = a[sampleIndex.s5].s5;
//        out.s6 = a[sampleIndex.s6].s6;
//        out.s7 = a[sampleIndex.s7].s7;
//        return out;
//    }
//
//    // no interpolation
//    template <typename DelayTimeType>
//    BufType read_no(DelayTimeType delay, int bufIndex = 0) {
//        using DelayTimeSimdType = typename simd::get_traits<DelayTimeType>::type;
//        constexpr usize DelayTimeSimdWidth = DelayTimeSimdType::count;
//        static_assert(SimdWidth == DelayTimeSimdWidth || DelayTimeSimdWidth == 1);
//        if constexpr (std::is_integral_v<typename DelayTimeSimdType::scalar_t>) {
//            return read(cast_i64(delay), bufIndex);
//        } else {
//            auto floorDelay = simd::floor(delay);
//            auto intDelay = cast_i64(floorDelay);
//            return read(intDelay, bufIndex);
//        }
//    }
//
//
//    // linear interpolation
//    template <typename DelayTimeType>
//    BufType read_lin(DelayTimeType delay, int bufIndex = 0) {
//        using DelayTimeSimdType = typename simd::get_traits<DelayTimeType>::type;
//        constexpr usize DelayTimeSimdWidth = DelayTimeSimdType::count;
//        static_assert(DelayTimeSimdWidth == SimdWidth || DelayTimeSimdWidth == 1);
//        static_assert(std::is_floating_point_v<typename DelayTimeSimdType::scalar_t>);
//
//        auto floorDelay = simd::floor(delay);
//        auto fracDelay = delay - floorDelay;
//        auto intDelay = cast_i64(floorDelay);
//
//        if constexpr (SimdWidth == 1) {
//            f32x2 y;
//            y[0] = read(intDelay    , bufIndex);
//            y[1] = read(intDelay + 1, bufIndex);
//            
//            return lerp(fracDelay, y);
//        } else {
//            BufType y0 = read(intDelay    , bufIndex);
//            BufType y1 = read(intDelay + 1, bufIndex);
//            
//            return lerp(simd::convert<ScalarType>(fracDelay), y0, y1);
//        }
//    }
//
//    // cubic interpolation
//    template <typename DelayTimeType>
//    BufType read_cubic(DelayTimeType delay, int bufIndex = 0) {
//        constexpr usize DelayTimeSimdWidth = simd::get_traits<DelayTimeType>::type::count;
//        static_assert(DelayTimeSimdWidth == SimdWidth || DelayTimeSimdWidth == 1);
//        auto floorDelay = simd::floor(delay);
//        auto fracDelay = delay - floorDelay;
//        auto intDelay = cast_i64(floorDelay);
//
//        if constexpr (SimdWidth == 1) {
//            f32x4 y;
//            y[0] = read(intDelay - 1, bufIndex);
//            y[1] = read(intDelay    , bufIndex);
//            y[2] = read(intDelay + 1, bufIndex);
//            y[3] = read(intDelay + 2, bufIndex);
//            
//            return cubicInterpolate(fracDelay, y);
//        } else {
//            BufType y0 = read(intDelay - 1, bufIndex);
//            BufType y1 = read(intDelay    , bufIndex);
//            BufType y2 = read(intDelay + 1, bufIndex);
//            BufType y3 = read(intDelay + 2, bufIndex);
//            
//            return cubicInterpolate(simd::convert<ScalarType>(fracDelay), y0, y1, y2, y3);
//        }
//    }
//};
//
//
//template <class SimdType>
//auto reduce_max(SimdType in) {
//    return simd::reduce_max(in);
//}
//
//template <class SimdType>
//auto reduce_min(SimdType in) {
//    return simd::reduce_min(in);
//}
//
//template <class SimdType>
//auto reduce_sum(SimdType in) {
//    return simd::reduce_add(in);
//}
//
//template <class SimdType>
//auto reduce_prod(SimdType in) {
//    constexpr const usize SimdWidth = simd::get_traits<SimdType>::type::count;
//    if constexpr (SimdWidth == 8) {
//        auto b = in.s0123 * in.s4567;
//        auto a = b.s01 * b.x23;
//        return a.s0 * a.s1;
//    } else if constexpr (SimdWidth == 4) {
//        auto a = in.s01 * in.x23;
//        return a.s0 * a.s1;
//    } else if constexpr (SimdWidth == 2) {
//        return in.s0 * in.s1;
//    } else {
//        return in;
//    }
//}
//
//template <usize N, class SimdType>
//auto reduce_max(SimdType* in) {
//    SimdType out = in[0];
//    for (usize i = 1; i < N; ++i) {
//        simd::max(out, in[i]);
//    }
//    return simd::reduce_max(out);
//}
//
//template <usize N, class SimdType>
//auto reduce_min(SimdType* in) {
//    SimdType out = in[0];
//    for (usize i = 1; i < N; ++i) {
//        simd::min(out, in[i]);
//    }
//    return simd::reduce_min(out);
//}
//
//template <usize N, class SimdType>
//auto reduce_sum(SimdType* in) {
//    SimdType out = in[0];
//    for (usize i = 1; i < N; ++i) {
//        out += in[i];
//    }
//    return simd::reduce_add(out);
//}
//
//template <usize N, class SimdType>
//auto reduce_prod(SimdType* in) {
//    
//    SimdType out = in[0];
//    constexpr const usize SimdWidth = simd::get_traits<SimdType>::type::count;
//    if constexpr (SimdWidth == 8) {
//        for (usize i = 1; i < N; ++i) {
//            out *= in[i];
//        }
//    }
//    return reduce_prod(out);
//}
//
//}

#endif /* tzpl_plugin_interface */
