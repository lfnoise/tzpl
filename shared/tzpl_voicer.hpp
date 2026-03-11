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
//  tzpl_voicer.hpp
//  shared
//
//  Polyphonic voice allocator template.
//  Extracted from engine/src/tzpl_test_plugins.hpp.
//

#ifndef tzpl_voicer_hpp
#define tzpl_voicer_hpp

#include "tzpl_plugin_abi.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

// Type aliases for standalone use (e.g., generated plugin code).
// The engine uses `long` for i64 (for SIMD compat), while
// other contexts may use `int64_t` (long long). We use `long` here
// to match the engine. To avoid redefinition errors, we only define
// if the including context hasn't already defined them.
#ifndef TZPL_VOICER_TYPES_DEFINED
#define TZPL_VOICER_TYPES_DEFINED
using f32 = float;
using f64 = double;
using i64 = long;
using u16 = unsigned short;
using u32 = unsigned int;
#endif

// ---------------------------------------------------------------------------
// Utility functions
// ---------------------------------------------------------------------------

constexpr int NextPowerOfTwo_(int k, int n) {
    if (n >= k) return n;
    if (n == (1 << 30)) return n;
    return NextPowerOfTwo_(k, n*2);
}

constexpr int NextPowerOfTwo(int k) {
    return NextPowerOfTwo_(k, 1);
}

inline f64 nnhz(f64 nn) { return 440. * exp2((nn - 69.)*(1./12.)); }

inline f64 calcDecay(f64 amount, f64 decaytime, f64 sampleRate)
{
    if (decaytime == 0.) {
        return 0.;
    } else if (decaytime > 0.) {
        return pow(amount, 1./(decaytime * sampleRate));
    } else if (decaytime < 0.) {
        return -pow(amount, 1./(-decaytime * sampleRate));
    } else {
        return 0.;
    }
}

inline f64 ampdb(f64 amp) { return 20. * log10(amp); }

// ---------------------------------------------------------------------------
// Voicer template — polyphonic voice allocator
// ---------------------------------------------------------------------------

enum class ShouldCache { no, yes };
struct VoicesInRows {};
struct VoicesInColumns {};

template <int MaxVoices, int NumParams, class RowsOrCols = VoicesInRows>
class Voicer {
    int noteID_[MaxVoices];
    i64 noteOnTime_[MaxVoices];
    i64 noteOffTime_[MaxVoices];
    static const int kCacheSize = NextPowerOfTwo(4*MaxVoices);
    static const int kCacheMask = kCacheSize - 1;
    u16 activeVoiceCache_[kCacheSize];
    int activeVoices_ = 0;
    int numNoteOns_ = 0;
    int numNoteOffs_ = 0;

    static constexpr bool voicesAreInColumns
        = std::is_same_v<RowsOrCols, VoicesInColumns>;

    using VoiceParamMatrix = std::conditional_t<voicesAreInColumns,
                                    f32[1+NumParams][MaxVoices],
                                    f32[MaxVoices][1+NumParams]>;

    VoiceParamMatrix* params_;

    int allocVoice() {
        // Find a slot to use for the new note.
        // Find the earliest note off, or if all notes are on, then find the earliest note on.
        i64 minNoteOffTime = std::numeric_limits<i64>::max();
        i64 minNoteOnTime = std::numeric_limits<i64>::max();
        int minNoteOffIndex = -1;
        int minNoteOnIndex = -1;
        for (int i = 0; i < MaxVoices; ++i) {
            if (get(i,0) > 0.) { // if gate is on.
                if (noteOnTime_[i] < minNoteOnTime) {
                    minNoteOnTime = noteOnTime_[i];
                    minNoteOnIndex = i;
                }
            } else { // gate is off.
                if (noteOffTime_[i] < minNoteOffTime) {
                    minNoteOffTime = noteOffTime_[i];
                    minNoteOffIndex = i;
                }
            }
        }
        int index = minNoteOffIndex >= 0 ? minNoteOffIndex : minNoteOnIndex;
        assert(index >= 0);
        return index;
    }
public:
    Voicer() {
        memset(noteID_, 0, sizeof(noteID_));
        memset(noteOnTime_, 0, sizeof(noteOnTime_));
        memset(noteOffTime_, 0, sizeof(noteOffTime_));
        memset(activeVoiceCache_, 0, sizeof(activeVoiceCache_));
    }

    int maxVoices() const { return MaxVoices; }
    int activeVoices() const { return activeVoices_; }
    int numNoteOns() const { return numNoteOns_; }
    int numNoteOffs() const { return numNoteOffs_; }
    int numParams() const { return NumParams; }
    f32* out() { return (f32*)params_; }

    void setParams(f32* p) {
        params_ = (VoiceParamMatrix*)p;
    }

    int findVoice(int noteID, ShouldCache shouldCache = ShouldCache::yes) {
        int cacheIndex = u32(noteID) & kCacheMask;
        int voiceIndex = activeVoiceCache_[cacheIndex];
        if (noteID == noteID_[voiceIndex]) return voiceIndex;

        if (noteID < 0) return -1;

        for (voiceIndex = 0; voiceIndex < MaxVoices; ++voiceIndex) {
            if (noteID_[voiceIndex] == noteID) {
                if (shouldCache == ShouldCache::yes) activeVoiceCache_[cacheIndex] = voiceIndex;
                return voiceIndex;
            }
        }
        return -1;
    }


    f32* getRow(int row) {
        return &(*params_)[row][0];
    }

    // get a parameter value
    f32 get(int voice, int param) const {
        if constexpr (voicesAreInColumns) {
            return (*params_)[param][voice];
        } else {
            return (*params_)[voice][param];
        }
    }

    // set a parameter value
    void set(int voice, int param, f32 value) {
        if constexpr (voicesAreInColumns) {
            (*params_)[param][voice] = value;
        } else {
            (*params_)[voice][param] = value;
        }
    }

    tzpl_SErr noteOn(i64 now, int noteID, int n, f32* params, int& voiceIndex) {
        if (noteID < 0) return tzpl_errNoteNotFound;
        voiceIndex = allocVoice();
        noteOnTime_[voiceIndex] = now;
        noteID_[voiceIndex] = noteID;
        int cacheIndex = u32(noteID) & kCacheMask;
        activeVoiceCache_[cacheIndex] = voiceIndex;
        ++activeVoices_;
        ++numNoteOns_;
        set(voiceIndex, 0, 1.f); // set the gate parameter on.
        setNoteParamRange(voiceIndex, 0, n, params);
        return tzpl_errNone;
    }

    tzpl_SErr noteOff(i64 now, int noteID) {
        int voiceIndex = findVoice(noteID, ShouldCache::no);
        if (voiceIndex < 0) return tzpl_errNoteNotFound;
        --activeVoices_;
        ++numNoteOffs_;
        noteOffTime_[voiceIndex] = now;
        set(voiceIndex, 0, 0.f); // set the gate parameter to off.
        return tzpl_errNone;
    }

    void allOff(i64 now) {
        for (int voiceIndex = 0; voiceIndex < MaxVoices; ++voiceIndex) {
            if (get(voiceIndex, 0) > 0.f) { // if the gate parameter is on.
                noteOffTime_[voiceIndex] = now;
                set(voiceIndex, 0, 0.f); // set the gate parameter to off.
            }
        }
        activeVoices_ = 0;
    }


    void setNoteParamRange(int voiceIndex, int first, int length, f32* params) {
        length = std::min(length, NumParams-first);
        for (int i = 0; i < length; ++i) {
            set(voiceIndex, i+first+1, params[i]);
        }
    }

    void setNoteParams(int voiceIndex, int n, tzpl_ParamPair* params) {
        for (int i = 0; i < n; ++i) {
            tzpl_ParamPair& p = params[i];
            if (p.index < 0 || p.index >= NumParams) continue;
            set(voiceIndex, p.index+1, p.value);
        }
    }
};

// ColumnVoicer is better for SIMD code where all voices are evaluated in parallel.
// RowVoicer is better for scalar code.

template <int MaxVoices, int NumParams>
using ColumnVoicer = Voicer<MaxVoices, NumParams, VoicesInColumns>;

template <int MaxVoices, int NumParams>
using RowVoicer    = Voicer<MaxVoices, NumParams, VoicesInRows>;

#endif // tzpl_voicer_hpp
