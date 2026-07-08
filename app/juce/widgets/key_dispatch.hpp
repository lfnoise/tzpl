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
//  key_dispatch.hpp
//  app (JUCE)
//
//  Global key bindings for widgets (ui.bindKey), the JUCE port of
//  widget_draw.cpp::dispatchWidgetKeys. A message-thread timer polls the
//  physical key state for each widget's keyChord: Buttons fire momentary
//  (down=1 / up=0), Toggles flip on press -- but only while no text field
//  owns keyboard focus, so typing in an editor never triggers a binding.
//
//  Polling (rather than KeyPress routing) mirrors the ImGui per-frame model
//  and works regardless of which component holds focus. Runs at ~60 Hz while
//  any binding exists, backing off to a slow re-check poll when none do, so
//  a document without key bindings costs nothing.
//

#ifndef key_dispatch_hpp
#define key_dispatch_hpp

#include <juce_events/juce_events.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace bridge { struct UIState; }

namespace tzplapp {

class ControlsDispatcher;

class KeyDispatch : public juce::Timer {
public:
    KeyDispatch(bridge::UIState& ui, ControlsDispatcher& dispatcher);

    // Begin polling. Idempotent.
    void start();

    void timerCallback() override;

    // Map a stored chord ("a".."z", "0".."9", "space") to a JUCE key code,
    // or -1 if unmappable. Public for the self-test.
    static int chordKeyCode(std::string const& chord);

    // Self-test: apply a press edge to every widget bound to `chord` (Toggle
    // flips, Button goes to 1) without a real key event; returns the resulting
    // value of the last matched widget, or -999 if none. Exercises the same
    // value + dirty path as the poll.
    double testPressChord(std::string const& chord);

private:
    // True while a TextInputTarget (editor / text field) holds focus.
    static bool textInputFocused();

    bridge::UIState& ui_;
    ControlsDispatcher& dispatcher_;
    // Per-widget last observed key-down state, for press/release edges.
    std::unordered_map<std::uint64_t, bool> down_;
    bool fast_ = false;   // current poll rate is the fast (bound) rate

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyDispatch)
};

}

#endif /* key_dispatch_hpp */
