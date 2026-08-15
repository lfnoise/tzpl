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
//  controls_window.cpp
//  app (JUCE)
//

#include "controls_window.hpp"

namespace tzplapp {

using juce::String;

ControlsWindow::Content::Content(bridge::UIState& ui, std::string const& panel,
                                 ControlsDispatcher& d)
    : canvas_(ui, panel, d)
{
    viewport_.setViewedComponent(&canvas_, false);
    viewport_.setScrollBarsShown(true, false);
    // Code created/removed widgets: re-size the canvas inside the viewport
    // (the canvas's parent is the viewport's holder, whose resized() would
    // do nothing).
    canvas_.onContentChanged = [this] { resized(); };
    addAndMakeVisible(viewport_);
}

void ControlsWindow::Content::resized() {
    viewport_.setBounds(getLocalBounds());
    canvas_.setSize(viewport_.getMaximumVisibleWidth(),
                    juce::jmax(canvas_.preferredHeight(getWidth()),
                               viewport_.getHeight()));
}

juce::Point<int> ControlsWindow::Content::preferredContentSize() const {
    return canvas_.preferredContentSize();
}

ControlsWindow::ControlsWindow(bridge::UIState& ui, std::string const& panel,
                               ControlsDispatcher& dispatcher)
    : juce::DocumentWindow(panel.empty() ? "Controls" : String(panel),
                           juce::Colours::darkgrey,
                           juce::DocumentWindow::closeButton),
      panel_(panel)
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    auto* content = new Content(ui, panel, dispatcher);
    setContentOwned(content, false);
    // Open fitted to the panel's widgets rather than at one fixed size.
    // The host (MainComponent) clamps to the display, picks a position
    // that doesn't cover other panel windows, and shows the window.
    setSize(content->preferredContentSize().x,
            content->preferredContentSize().y);
}

}
