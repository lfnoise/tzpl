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

#include "tzpl_spectrum.hpp"

#include "tzpl_fft.hpp"

#include <algorithm>
#include <cmath>

namespace bridge {

SpectrumEngine::~SpectrumEngine() {
    for (auto& [size, setup] : setups_) synthdef::tzpl_fft_destroy(setup);
}

synthdef::JscsFFTSetup* SpectrumEngine::setupFor(int fftSize) {
    auto it = setups_.find(fftSize);
    if (it != setups_.end()) return it->second;
    auto* s = synthdef::tzpl_fft_create(fftSize);
    setups_.emplace(fftSize, s);
    auto& win = windows_[fftSize];
    win.resize((size_t)fftSize);
    synthdef::tzpl_window_hann(win.data(), fftSize);
    return s;
}

void SpectrumEngine::analyze(std::vector<float> const& ring, int chans, int ch,
                             int fftSize, float floorDb, float fallDb,
                             std::vector<float>& out) {
    if (fftSize < 4 || (fftSize & (fftSize - 1)) != 0) return;
    if (chans < 1) chans = 1;

    int const bins = numBins(fftSize);
    if ((int)out.size() != bins) out.assign((size_t)bins, floorDb);

    // Fall first, so a stopped signal decays to the floor even on the ticks
    // where there aren't enough frames to analyze.
    for (auto& v : out) v = std::max(floorDb, v - fallDb);

    int const framesAvail = (int)(ring.size() / (size_t)chans);
    if (framesAvail < fftSize) return;

    auto* setup = setupFor(fftSize);
    auto const& win = windows_[fftSize];

    frame_.resize((size_t)fftSize);
    spec_.resize((size_t)fftSize);

    size_t const base = (size_t)(framesAvail - fftSize) * (size_t)chans;
    for (int i = 0; i < fftSize; ++i) {
        float const* frame = ring.data() + base + (size_t)i * (size_t)chans;
        float v;
        if (ch >= 0 && ch < chans) {
            v = frame[ch];
        } else {
            float sum = 0.f;
            for (int c = 0; c < chans; ++c) sum += frame[c];
            v = sum / (float)chans;
        }
        frame_[(size_t)i] = v * win[(size_t)i];
    }

    synthdef::tzpl_fft_forward(setup, frame_.data(), spec_.data());

    // Packed split-complex: [re0..re(N/2-1), im0..im(N/2-1)], with re[0] = DC
    // and im[0] = Nyquist. Scale so a full-scale sine reads 0 dBFS: a real
    // sine of amplitude A lands half its energy in each of the +/- bins, and
    // the Hann window has a coherent gain of 0.5.
    int const halfN = fftSize / 2;
    float const cg = 0.5f;                                   // Hann coherent gain
    float const scale = 2.0f / ((float)fftSize * cg);
    float const dcScale = scale * 0.5f;                      // no mirror bin to fold in

    auto toDb = [&](float mag) {
        return 20.0f * std::log10(std::max(mag, 1e-9f));
    };
    auto keep = [&](int bin, float db) {
        db = std::max(db, floorDb);
        if (db > out[(size_t)bin]) out[(size_t)bin] = db;
    };

    keep(0, toDb(std::fabs(spec_[0]) * dcScale));
    for (int k = 1; k < halfN; ++k) {
        float re = spec_[(size_t)k];
        float im = spec_[(size_t)(halfN + k)];
        keep(k, toDb(std::hypot(re, im) * scale));
    }
    keep(halfN, toDb(std::fabs(spec_[(size_t)halfN]) * dcScale));
}

} // namespace bridge
