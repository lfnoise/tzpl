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
//  spectrum_test.cpp
//  app/tests
//
//  Tests for bridge::SpectrumEngine -- the host-side analyzer behind Spectrum
//  widgets. Checks bin placement, dBFS normalization (a full-scale sine must
//  read 0 dB), channel selection, and the per-tick fall.
//
//  Pure math over a std::vector: no engine, no GUI, no audio device.
//

#include "tzpl_spectrum.hpp"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

static int gPassed = 0;
static int gFailed = 0;

static void check(bool ok, std::string const& what) {
    if (ok) { std::printf("  PASS: %s\n", what.c_str()); ++gPassed; }
    else    { std::printf("  FAIL: %s\n", what.c_str()); ++gFailed; }
}

static constexpr float kFloorDb = -96.0f;
static constexpr float kFallDb = 0.7f;

// Interleaved `chans`-channel ring; the sine goes only into `sineCh`
// (< 0 = every channel).
static std::vector<float> makeSine(double freq, double sr, int frames,
                                   float amp = 1.0f, int chans = 1,
                                   int sineCh = -1) {
    std::vector<float> ring((size_t)frames * (size_t)chans, 0.0f);
    for (int i = 0; i < frames; ++i) {
        float v = amp * (float)std::sin(2.0 * std::numbers::pi * freq
                                        * (double)i / sr);
        for (int c = 0; c < chans; ++c) {
            if (sineCh < 0 || sineCh == c)
                ring[(size_t)i * (size_t)chans + (size_t)c] = v;
        }
    }
    return ring;
}

static int argMax(std::vector<float> const& v) {
    int best = 0;
    for (int i = 1; i < (int)v.size(); ++i) if (v[(size_t)i] > v[(size_t)best]) best = i;
    return best;
}

// A full-scale sine on an exact bin centre must land in that bin, at 0 dBFS.
// This validates the window, the FFT packing, and the normalization together
// -- get any one of them wrong and the level or the bin moves.
static void test_sine_bin_and_level() {
    std::printf("Test: spectrum bin placement and level\n");

    int const N = 2048;
    double const sr = 44100.0;
    // Exact bin centre, so there is no scalloping loss to account for.
    int const wantBin = 46;
    double const freq = (double)wantBin * sr / (double)N;

    bridge::SpectrumEngine eng;
    std::vector<float> out;
    auto ring = makeSine(freq, sr, N * 2);
    eng.analyze(ring, 1, -1, N, kFloorDb, kFallDb, out);

    check((int)out.size() == bridge::SpectrumEngine::numBins(N),
          "numBins magnitudes are produced");
    int peak = argMax(out);
    check(peak == wantBin,
          "the peak lands in the expected bin (got " + std::to_string(peak)
              + ", want " + std::to_string(wantBin) + ")");
    check(std::fabs(out[(size_t)wantBin]) < 0.5f,
          "a full-scale sine reads 0 dBFS (got "
              + std::to_string(out[(size_t)wantBin]) + ")");

    // Half amplitude is 6 dB down.
    bridge::SpectrumEngine eng2;
    std::vector<float> half;
    auto ringHalf = makeSine(freq, sr, N * 2, 0.5f);
    eng2.analyze(ringHalf, 1, -1, N, kFloorDb, kFallDb, half);
    check(std::fabs(half[(size_t)wantBin] + 6.02f) < 0.5f,
          "half amplitude reads -6 dBFS (got "
              + std::to_string(half[(size_t)wantBin]) + ")");
}

static void test_silence() {
    std::printf("Test: spectrum of silence\n");

    int const N = 1024;
    bridge::SpectrumEngine eng;
    std::vector<float> out;
    std::vector<float> ring((size_t)N * 2, 0.0f);
    eng.analyze(ring, 1, -1, N, kFloorDb, kFallDb, out);

    bool allFloor = true;
    for (float v : out) if (v > kFloorDb + 1e-3f) allFloor = false;
    check(allFloor, "every bin sits at the floor");
}

// Validates de-interleaving: with the sine only in channel 1, analyzing
// channel 0 must see nothing.
static void test_channel_selection() {
    std::printf("Test: spectrum channel selection\n");

    int const N = 1024;
    double const sr = 44100.0;
    int const wantBin = 32;
    double const freq = (double)wantBin * sr / (double)N;
    auto ring = makeSine(freq, sr, N * 2, 1.0f, /*chans=*/2, /*sineCh=*/1);

    bridge::SpectrumEngine eng;
    std::vector<float> ch0, ch1;
    eng.analyze(ring, 2, /*ch=*/0, N, kFloorDb, kFallDb, ch0);
    eng.analyze(ring, 2, /*ch=*/1, N, kFloorDb, kFallDb, ch1);

    bool ch0Silent = true;
    for (float v : ch0) if (v > kFloorDb + 1e-3f) ch0Silent = false;
    check(ch0Silent, "the silent channel reads the floor");
    check(argMax(ch1) == wantBin, "the signal channel finds the peak bin");
    check(std::fabs(ch1[(size_t)wantBin]) < 0.5f,
          "the signal channel reads full scale");

    // Mixing both channels halves the amplitude: -6 dB.
    bridge::SpectrumEngine eng2;
    std::vector<float> mixed;
    eng2.analyze(ring, 2, /*ch=*/-1, N, kFloorDb, kFallDb, mixed);
    check(std::fabs(mixed[(size_t)wantBin] + 6.02f) < 0.5f,
          "the channel mean is 6 dB down when only one channel has signal");
}

// Peaks fall by exactly fallDb per tick and stop at the floor, so a stopped
// signal decays smoothly instead of vanishing between polls.
static void test_decay() {
    std::printf("Test: spectrum decay\n");

    int const N = 1024;
    double const sr = 44100.0;
    int const bin = 32;
    double const freq = (double)bin * sr / (double)N;

    bridge::SpectrumEngine eng;
    std::vector<float> out;
    auto loud = makeSine(freq, sr, N * 2);
    eng.analyze(loud, 1, -1, N, kFloorDb, kFallDb, out);
    float start = out[(size_t)bin];

    // Too few frames to analyze: the previous values just keep falling.
    std::vector<float> tiny((size_t)8, 0.0f);
    int const ticks = 10;
    for (int i = 0; i < ticks; ++i)
        eng.analyze(tiny, 1, -1, N, kFloorDb, kFallDb, out);

    float want = start - (float)ticks * kFallDb;
    check(std::fabs(out[(size_t)bin] - want) < 1e-3f,
          "the bin falls exactly fallDb per tick (got "
              + std::to_string(out[(size_t)bin]) + ", want "
              + std::to_string(want) + ")");

    for (int i = 0; i < 1000; ++i)
        eng.analyze(tiny, 1, -1, N, kFloorDb, kFallDb, out);
    check(out[(size_t)bin] == kFloorDb, "the fall clamps at the floor");
}

// DC and Nyquist share one slot in the packed FFT layout; make sure they are
// unpacked into the right bins rather than aliasing onto each other.
static void test_dc_and_nyquist() {
    std::printf("Test: spectrum DC and Nyquist bins\n");

    int const N = 1024;
    int const bins = bridge::SpectrumEngine::numBins(N);

    bridge::SpectrumEngine eng;
    std::vector<float> out;
    std::vector<float> dc((size_t)N * 2, 0.5f);
    eng.analyze(dc, 1, -1, N, kFloorDb, kFallDb, out);
    check(argMax(out) == 0, "a DC signal peaks in bin 0");
    check(out[(size_t)(bins - 1)] < kFloorDb + 60.0f,
          "DC does not leak into the Nyquist bin");

    // Alternating +/- is exactly Nyquist. The Hann window spreads it evenly
    // across the top two bins (the mirror of bin N/2 folds back onto N/2-1),
    // so assert the energy is at the top at the right LEVEL rather than
    // pinning it to one bin.
    bridge::SpectrumEngine eng2;
    std::vector<float> nyq((size_t)N * 2);
    for (size_t i = 0; i < nyq.size(); ++i) nyq[i] = (i % 2) ? -0.5f : 0.5f;
    std::vector<float> out2;
    eng2.analyze(nyq, 1, -1, N, kFloorDb, kFallDb, out2);
    check(argMax(out2) >= bins - 2, "a Nyquist signal peaks at the top of the spectrum");
    check(std::fabs(out2[(size_t)(bins - 1)] + 6.02f) < 0.5f,
          "the Nyquist bin reads the right level (got "
              + std::to_string(out2[(size_t)(bins - 1)]) + ")");
    check(out2[(size_t)(bins - 3)] < kFloorDb + 1e-3f,
          "Nyquist stays confined to the top two bins");
    check(out2[0] < kFloorDb + 60.0f, "Nyquist does not leak into DC");
}

int main() {
    std::printf("=== Spectrum analyzer tests ===\n\n");

    test_sine_bin_and_level();
    test_silence();
    test_channel_selection();
    test_decay();
    test_dc_and_nyquist();

    std::printf("\n=== %d passed, %d failed ===\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
