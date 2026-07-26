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
//  controls_dispatch.hpp
//  app (JUCE)
//
//  Per-tick widget event dispatch + tap polling, the JUCE port of
//  ControlsPanel::dispatch (controls_panel.cpp) -- itself toolkit-free.
//  Runs on a juce::Timer at ~30 Hz while anything needs it (tap-backed
//  widget visible, gesture in flight, dirty flag pending), stopping when
//  the registry is idle so a quiet app paints and computes nothing.
//
//  Lock order (from tzpl_ui_state.hpp): nrtvm.mtx BEFORE UIState::mtx.
//  Tap reads take UIState::mtx then the engine NRT lock (reverse never
//  happens); onChange delivery try_locks nrtvm.mtx first.
//

#ifndef controls_dispatch_hpp
#define controls_dispatch_hpp

#include <juce_events/juce_events.h>

#include "tzpl_spectrum.hpp"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace bridge { struct AppContext; }

namespace tzplapp {

class ControlsDispatcher : public juce::Timer {
public:
    explicit ControlsDispatcher(bridge::AppContext& appCtx);

    // Run one dispatch pass immediately (also invoked by the timer). Repaints
    // tap-backed widgets via the repaint hook.
    void tick();

    void timerCallback() override { tick(); }

    // Ensure the timer is running (called on user interaction / widget show).
    void ensureRunning();

    // Queue removal of these panels' widgets (with engine untap) -- the same
    // path as closing a floating panel window / closing a notebook document.
    void queuePanelRemoval(std::vector<std::string> const& panels);

    // Called each tick after tap polling so the host can repaint the
    // meter/scope widgets whose data just advanced.
    std::function<void()> onTapsAdvanced;

    // Called with the {panel, name} of each widget whose gesture (slider
    // drag, XY sweep, toggle flip) finished this tick, buttons excluded --
    // the notebook turns claimed-panel gestures into one history commit.
    std::function<void(std::vector<std::pair<std::string, std::string>> const&)>
        onGesturesEnded;

private:
    // True if any widget still has a pending engine/callback event or a
    // visible tap -- keeps the timer alive; false lets it stop.
    bool hasWork();

    bridge::AppContext& appCtx_;
    std::vector<std::string> pendingClosedPanels_;
    int idleTicks_ = 0;
    bool tapsVisibleLastTick_ = false;
    // FFT setups + scratch for Spectrum widgets, shared across all of them
    // (cached by size). Used only from tick(), on the message thread.
    bridge::SpectrumEngine spectrum_;
};

}

#endif /* controls_dispatch_hpp */
