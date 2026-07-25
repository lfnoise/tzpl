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
//  tzpl_ui_taps.hpp
//  bridge
//
//  Engine-tap plumbing for UI widgets, shared by both frontends. The ImGui
//  and JUCE control dispatchers ran byte-identical copies of this logic and
//  had begun to drift, so it lives here once.
//
//  Threading: bundle submission takes the engine's NRT lock, so the untap
//  helpers must be called WITHOUT UIState::mtx held. pollWidgetTaps takes
//  UIState::mtx itself and calls the engine's lock-free tap readers under it
//  -- the reverse order never occurs, since no engine code touches UIState.
//

#ifndef tzpl_ui_taps_hpp
#define tzpl_ui_taps_hpp

#include "tzpl_ui_state.hpp"

#include <string>
#include <utility>
#include <vector>

namespace engine { struct Engine; }

namespace bridge {

// (tapID, silo) pairs, as carried by UIWidget::tapID / tapSilo.
using TapRef = std::pair<long, int>;

// Remove one engine tap. No-op for tapID 0 (= "no tap") or a null engine.
// Must NOT be called with UIState::mtx held.
void untapWidget(engine::Engine* e, long tapID, int silo);

// Remove several taps in turn. Must NOT be called with UIState::mtx held.
void untapWidgets(engine::Engine* e, std::vector<TapRef> const& taps);

// Erase every widget belonging to any of `panels` (each entry is a root; its
// "root/sub" sub-panels go with it) and return the taps they held, for the
// caller to untap after releasing the lock. Takes UIState::mtx itself.
std::vector<TapRef> removePanelWidgets(UIState& ui,
                                       std::vector<std::string> const& panels);

class SpectrumEngine;

// Poll every tap-backed widget: Meter gets rms/peak in values[0]/[1], Scope
// and Spectrum append drained samples to their ring (trimmed to whole
// interleaved frames), and Spectrum additionally analyzes it through `fft`
// (pass null to skip the analysis). Takes UIState::mtx. Returns true if any
// tap-backed widget exists, which the frontends use to decide whether they
// need continuous frames.
bool pollWidgetTaps(UIState& ui, engine::Engine* e, SpectrumEngine* fft);

} // namespace bridge

#endif /* tzpl_ui_taps_hpp */
