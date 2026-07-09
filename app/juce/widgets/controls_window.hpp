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
//  controls_window.hpp
//  app (JUCE)
//
//  A floating window hosting one panel's widgets (a PanelCanvas in a
//  Viewport). One per `ui` panel NOT claimed by the open notebook document
//  -- the JUCE port of the ImGui Controls windows. Closing it removes the
//  panel's widgets (queued on the dispatcher), matching the ImGui behavior.
//

#ifndef controls_window_hpp
#define controls_window_hpp

#include "panel_canvas.hpp"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <string>

namespace bridge { struct UIState; }

namespace tzplapp {

class ControlsDispatcher;

class ControlsWindow : public juce::DocumentWindow {
public:
    ControlsWindow(bridge::UIState& ui, std::string const& panel,
                   ControlsDispatcher& dispatcher);

    void closeButtonPressed() override { if (onClose) onClose(); }

    std::string const& panel() const { return panel_; }

    // Invoked when the user closes the window (host removes the widgets).
    std::function<void()> onClose;

private:
    std::string panel_;

    class Content : public juce::Component {
    public:
        Content(bridge::UIState& ui, std::string const& panel,
                ControlsDispatcher& d);
        void resized() override;
    private:
        juce::Viewport viewport_;
        PanelCanvas canvas_;
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlsWindow)
};

}

#endif /* controls_window_hpp */
