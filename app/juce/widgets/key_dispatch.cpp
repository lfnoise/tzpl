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
//  key_dispatch.cpp
//  app (JUCE)
//

#include "key_dispatch.hpp"
#include "controls_dispatch.hpp"
#include "tzpl_ui_state.hpp"
#include <juce_gui_basics/juce_gui_basics.h>
#include <mutex>

namespace tzplapp {

namespace {
constexpr int kFastHz = 60;   // responsive polling while bindings exist
constexpr int kSlowHz = 8;    // idle re-check for new bindings

void markDirty(bridge::UIWidget& w) {
    w.dirtyEngine = true;
    w.dirtyCallback = true;
}
}

KeyDispatch::KeyDispatch(bridge::UIState& ui, ControlsDispatcher& dispatcher)
    : ui_(ui), dispatcher_(dispatcher) {}

void KeyDispatch::start() {
    if (!isTimerRunning()) { fast_ = false; startTimerHz(kSlowHz); }
}

int KeyDispatch::chordKeyCode(std::string const& chord) {
    if (chord == "space") return juce::KeyPress::spaceKey;
    if (chord.size() != 1) return -1;
    char c = chord[0];
    if (c >= 'a' && c <= 'z') return 'A' + (c - 'a');  // JUCE letter code
    if (c >= '0' && c <= '9') return c;
    return -1;
}

double KeyDispatch::testPressChord(std::string const& chord) {
    std::string c = chord;
    for (auto& ch : c) ch = (char)std::tolower((unsigned char)ch);
    double result = -999.0;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(ui_.mtx);
        for (auto& wp : ui_.widgets) {
            bridge::UIWidget& w = *wp;
            if (w.keyChord != c || w.values.empty()) continue;
            if (w.kind == bridge::UIWidgetKind::Toggle) {
                w.values[0] = w.values[0] != 0.0 ? 0.0 : 1.0;
                markDirty(w); w.gestureEnded = true; changed = true;
            } else if (w.kind == bridge::UIWidgetKind::Button) {
                w.values[0] = 1.0; markDirty(w); changed = true;
            }
            result = w.values[0];
        }
    }
    if (changed) dispatcher_.ensureRunning();
    return result;
}

bool KeyDispatch::textInputFocused() {
    auto* f = juce::Component::getCurrentlyFocusedComponent();
    return dynamic_cast<juce::TextInputTarget*>(f) != nullptr;
}

void KeyDispatch::timerCallback() {
    // A focused text field or an inactive app stands the bindings down; any
    // key held across that transition is released so a Button can't stick.
    bool suppress = textInputFocused()
                 || !juce::Process::isForegroundProcess();

    bool changed = false;
    bool anyBound = false;
    {
        std::lock_guard<std::mutex> lock(ui_.mtx);
        for (auto& wp : ui_.widgets) {
            bridge::UIWidget& w = *wp;
            if (w.keyChord.empty()) continue;
            anyBound = true;
            int kc = chordKeyCode(w.keyChord);
            if (kc < 0) continue;

            bool isDown = !suppress && juce::KeyPress::isKeyCurrentlyDown(kc);
            bool prev = down_[w.id];
            if (w.kind == bridge::UIWidgetKind::Button) {
                if (isDown && !prev && !w.values.empty()) {
                    w.values[0] = 1.0; markDirty(w); changed = true;
                } else if (!isDown && prev && !w.values.empty()) {
                    w.values[0] = 0.0; markDirty(w); changed = true;
                }
            } else if (w.kind == bridge::UIWidgetKind::Toggle) {
                if (isDown && !prev && !w.values.empty()) {
                    w.values[0] = w.values[0] != 0.0 ? 0.0 : 1.0;
                    markDirty(w);
                    w.gestureEnded = true;  // one history commit per flip
                    changed = true;
                }
            }
            down_[w.id] = isDown;
        }
    }

    if (changed) dispatcher_.ensureRunning();

    // Adaptive rate: poll fast while any binding exists (matches the ImGui
    // frame-rate dispatch), else drop to an idle re-check.
    bool wantFast = anyBound;
    if (wantFast != fast_) {
        fast_ = wantFast;
        startTimerHz(fast_ ? kFastHz : kSlowHz);
    }
}

}
