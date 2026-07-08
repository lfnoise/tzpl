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
//  panel_canvas.hpp
//  app (JUCE)
//
//  Renders all widgets of one panel (and its sub-panels) from the ui
//  registry. Reconciles WidgetComponents against UIState by widget id on a
//  light timer -- widgets appear/disappear as code creates/removes them --
//  and lays them out top-to-bottom in registry (seq) order. Arrange mode
//  (fx/fy/fw/fh placement, drag/resize) is M5; this is the flow layout.
//

#ifndef panel_canvas_hpp
#define panel_canvas_hpp

#include "widget_component.hpp"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace bridge { struct UIState; }

namespace tzplapp {

class ControlsDispatcher;

class PanelCanvas : public juce::Component, private juce::Timer {
public:
    PanelCanvas(bridge::UIState& ui, std::string panel,
                ControlsDispatcher& dispatcher);
    ~PanelCanvas() override;

    void resized() override;

    // The total height the current widget set needs at this width (so a
    // notebook panel cell can grow to fit).
    int preferredHeight(int width) const;

    // Reconcile now (also driven by the internal timer). Returns true if the
    // set of widgets changed (host may need to relayout for a new height).
    bool reconcile();

private:
    void timerCallback() override;
    void layOutWidgets();

    bridge::UIState& ui_;
    std::string panel_;
    ControlsDispatcher& dispatcher_;
    // id -> component, plus the seq-ordered id list from the last reconcile.
    std::unordered_map<std::uint64_t, std::unique_ptr<WidgetComponent>> widgets_;
    std::vector<std::uint64_t> order_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelCanvas)
};

}

#endif /* panel_canvas_hpp */
