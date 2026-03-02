//
//  jscs_test_plugins.hpp
//  audio engine
//
//  Test/example plugins: SinOsc, AddOp, MulOp, VoicerTest.
//  Also contains the Voicer polyphonic voice management template.
//

#ifndef jscs_test_plugins_h
#define jscs_test_plugins_h

#include "jscs_client_interface.hpp"
#include "jscs_node.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cmath>
#include <limits>
#include <type_traits>

namespace engine {

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

f32 bwarp(f32 x, f32 w);

// ---------------------------------------------------------------------------
// Voicer template — polyphonic voice allocator
// ---------------------------------------------------------------------------

enum class ShouldCache { no, yes };
struct VoicesInRows {};
struct VoicesInColumns {};

template <int MaxVoices, int NumParams, class RowsOrCols>
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
        i64 minNoteOffTime = std::numeric_limits<i64>::max();
        i64 minNoteOnTime = std::numeric_limits<i64>::max();
        int minNoteOffIndex = -1;
        int minNoteOnIndex = -1;
        for (int i = 0; i < MaxVoices; ++i) {
            if (get(i,0) > 0.) { // gate is on.
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
    f32* out() { return params_; }

    void setParams(f32* p) {
        params_ = (VoiceParamMatrix*)p;
    }

    int findVoice(int noteID, ShouldCache shouldCache = ShouldCache::yes) {
        int cacheIndex = u32(noteID) & kCacheMask;
        int voiceIndex = activeVoiceCache_[cacheIndex];
        if (noteID == noteID_[voiceIndex]) return voiceIndex;

        if (noteID < 0) return -1;

        printf("note cache miss %d\n", noteID);

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

    f32 get(int voice, int param) const {
        if constexpr (voicesAreInColumns) {
            return (*params_)[param][voice];
        } else {
            return (*params_)[voice][param];
        }
    }
    void set(int voice, int param, f32 value) {
        if constexpr (voicesAreInColumns) {
            (*params_)[param][voice] = value;
        } else {
            (*params_)[voice][param] = value;
        }
    }

    jscs_SErr noteOn(i64 now, int noteID, int n, f32* params, int& voiceIndex) {
        if (noteID < 0) return jscs_errNoteNotFound;
        voiceIndex = allocVoice();
        noteOnTime_[voiceIndex] = now;
        noteID_[voiceIndex] = noteID;
        int cacheIndex = u32(noteID) & kCacheMask;
        activeVoiceCache_[cacheIndex] = voiceIndex;
        ++activeVoices_;
        ++numNoteOns_;
        set(voiceIndex, 0, 1.f);
        setNoteParamRange(voiceIndex, 0, n, params);
        return jscs_errNone;
    }

    jscs_SErr noteOff(i64 now, int noteID) {
        int voiceIndex = findVoice(noteID, ShouldCache::no);
        if (voiceIndex < 0) return jscs_errNoteNotFound;
        --activeVoices_;
        ++numNoteOffs_;
        noteOffTime_[voiceIndex] = now;
        set(voiceIndex, 0, 0.f);
        return jscs_errNone;
    }

    void allOff(i64 now) {
        for (int voiceIndex = 0; voiceIndex < MaxVoices; ++voiceIndex) {
            if (get(voiceIndex, 0) > 0.f) {
                noteOffTime_[voiceIndex] = now;
                set(voiceIndex, 0, 0.f);
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

    void setNoteParams(int voiceIndex, int n, jscs_ParamPair* params) {
        for (int i = 0; i < n; ++i) {
            jscs_ParamPair& p = params[i];
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

// ---------------------------------------------------------------------------
// Plugin registration functions
// ---------------------------------------------------------------------------

void createSineNode(Engine* e);
void createAddOpNode(Engine* e);
void createMulOpNode(Engine* e);
void createVoicerTestNode(Engine* e);

} // namespace engine

#endif // jscs_test_plugins_h
