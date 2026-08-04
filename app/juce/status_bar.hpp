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
//  status_bar.hpp
//  app (JUCE)
//
//  Always-visible performance strip along the bottom of the main window:
//  DSP load, master level, and a LATCHING dropout indicator. Expands into a
//  detail panel with per-silo timings, queue depths, GC counters and device
//  telemetry.
//
//  Deliberately never takes keyboard focus: it sits under a text editor, and
//  a click here must not steal the caret.
//

#ifndef status_bar_hpp
#define status_bar_hpp

#include "tzpl_client_interface.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>

namespace bridge { struct AppContext; }

namespace tzplapp {

class StatusBar : public juce::Component, private juce::Timer {
public:
    explicit StatusBar(bridge::AppContext& appCtx);

    // Height the owner should give us: one row collapsed, plus the detail
    // grid when expanded.
    int preferredHeight() const;

    void paint(juce::Graphics& g) override;
    void mouseDown(juce::MouseEvent const& e) override;
    void mouseDrag(juce::MouseEvent const& e) override;
    void mouseUp(juce::MouseEvent const& e) override;
    void visibilityChanged() override;

    // Called with one line when a new dropout is detected (rate-limited to
    // one per second, so a dropout storm can't flood the log).
    std::function<void(std::string const&)> onDropout;
    // Called when the expanded/collapsed state changes, so the owner can
    // re-run its layout.
    std::function<void()> onHeightChanged;

private:
    void timerCallback() override;
    void refreshStats();
    juce::Rectangle<int> compactRow() const;
    juce::Rectangle<int> xrunArea() const;
    // The master meter's clip square: click it to clear the clip latch.
    juce::Rectangle<int> clipArea() const;
    // Master gain slider track and the mute button, always in the compact row.
    juce::Rectangle<int> gainArea() const;
    juce::Rectangle<int> muteArea() const;
    // Panic buttons in the expanded detail panel: all notes off (all nodes),
    // clear all schedulers, disconnect the output node, free all nodes.
    juce::Rectangle<int> panicRow() const;
    void panicButtonRects(juce::Rectangle<int>* out4) const;
    void setGainFromX(int x);
    // Run a queued engine op (allNotesOffAll / disconnectNode / freeAllNodes)
    // as one bundle per silo, dispatched immediately.
    void forEachSiloBundle(std::function<void()> const& queueOps);

    bridge::AppContext& appCtx_;
    engine::EngineStats stats_;
    bool expanded_ = false;
    bool draggingGain_ = false;

    // Dropout latch: once tripped it stays lit until the user clicks to
    // reset. A dropout must never quietly disappear.
    bool dropoutLatched_ = false;
    unsigned long long lastDropoutTotal_ = 0;
    unsigned long long lastClipCount_ = 0;
    bool clipLatched_ = false;
    int flashTicks_ = 0;       // amber background countdown
    int logCooldown_ = 0;      // ticks until another console line is allowed
    int statsTick_ = 0;        // divides the timer down for the full snapshot

    // NRT VM GC counters, refreshed opportunistically (try_lock only -- a
    // long compile must never stall the GUI). Stale values are shown rather
    // than blocking.
    unsigned long long nrtGcSteps_ = 0, nrtGcCycles_ = 0, nrtGcMaxNanos_ = 0;
    bool nrtGcValid_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};

}

#endif /* status_bar_hpp */
