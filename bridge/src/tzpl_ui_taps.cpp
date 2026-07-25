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

#include "tzpl_ui_taps.hpp"

#include "tzpl_client_interface.hpp"
#include "tzpl_spectrum.hpp"

#include <algorithm>
#include <cstdio>

namespace bridge {

void untapWidget(engine::Engine* e, long tapID, int silo) {
    if (!e || tapID == 0) return;
    tzpl_SErr err = engine::begin(e);
    if (err == tzpl_errNone) {
        engine::untap(tapID);
        err = engine::go(silo);
    }
    if (err != tzpl_errNone) {
        std::fprintf(stderr, "ui: untap %ld failed (%d)\n", tapID, (int)err);
    }
}

void untapWidgets(engine::Engine* e, std::vector<TapRef> const& taps) {
    if (!e) return;
    for (auto [tapID, silo] : taps) untapWidget(e, tapID, silo);
}

std::vector<TapRef> removePanelWidgets(UIState& ui,
                                       std::vector<std::string> const& panels) {
    std::vector<TapRef> taps;
    std::lock_guard<std::mutex> lock(ui.mtx);
    for (auto const& panel : panels) {
        // Sub-panels ("panel/...") go with their root.
        for (auto& w : ui.widgets) {
            if (panelUnderRoot(w->panel, panel) && w->tapID)
                taps.push_back({w->tapID, w->tapSilo});
        }
        std::erase_if(ui.widgets, [&](auto const& w) {
            return panelUnderRoot(w->panel, panel);
        });
    }
    return taps;
}

// Spectrum decay per tick. The dispatchers tick at 30 Hz, so ~21 dB/s: fast
// enough to track a stopping note, slow enough not to flicker.
static constexpr float kSpectrumFallDb = 0.7f;
static constexpr float kSpectrumFloorDb = -96.0f;

bool pollWidgetTaps(UIState& ui, engine::Engine* e, SpectrumEngine* fft) {
    if (!e) return false;

    double sampleRate = 0.;
    bool anyTaps = false;
    std::lock_guard<std::mutex> lock(ui.mtx);
    for (auto& wp : ui.widgets) {
        UIWidget& w = *wp;
        if (w.tapID == 0) continue;
        anyTaps = true;

        if (w.kind == UIWidgetKind::Meter) {
            // A Meter restored from an older snapshot can arrive with fewer
            // than two values; widen before writing rms/peak.
            if (w.values.size() < 2) w.values.resize(2, 0.0);
            w.values[0] = engine::tapRms(e, w.tapID);
            w.values[1] = engine::tapPeak(e, w.tapID);
        } else if (w.kind == UIWidgetKind::Scope
                   || w.kind == UIWidgetKind::Spectrum) {
            int chans = std::max(1, engine::tapChans(e, w.tapID));
            w.scopeChans = chans;
            // Drain and trim in whole interleaved frames so the ring stays
            // frame-aligned.
            float buf[4096];
            int want = (4096 / chans) * chans;
            int n = engine::tapDrain(e, w.tapID, buf, want);
            if (n > 0) {
                w.scopeRing.insert(w.scopeRing.end(), buf, buf + n);
                size_t maxRing = (size_t)8192 * chans;
                if (w.scopeRing.size() > maxRing) {
                    size_t excess = w.scopeRing.size() - maxRing;
                    excess = ((excess + chans - 1) / chans) * chans;
                    w.scopeRing.erase(w.scopeRing.begin(),
                                      w.scopeRing.begin() + excess);
                }
            }
            if (w.kind == UIWidgetKind::Spectrum && fft) {
                // One engine-format read per poll, not per widget.
                if (sampleRate == 0.) {
                    engine::EngineStats st;
                    getEngineStats(e, st);
                    sampleRate = st.sampleRate;
                }
                if (sampleRate > 0.) w.spectrumSampleRate = (float)sampleRate;
                fft->analyze(w.scopeRing, chans, w.scopeChannel, w.fftSize,
                             kSpectrumFloorDb, kSpectrumFallDb, w.spectrum);
            }
        }
    }
    return anyTaps;
}

} // namespace bridge
