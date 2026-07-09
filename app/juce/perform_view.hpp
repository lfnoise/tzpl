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
//  perform_view.hpp
//  app (JUCE)
//
//  Perform mode: a full-window overlay showing one panel cell at a time
//  (the HyperCard-card model), its widgets enlarged by a fixed scale and
//  the layout locked. A tab per panel switches cards. Esc or the Exit
//  button leaves. Mirrors NotebookPanel::drawPerform.
//

#ifndef perform_view_hpp
#define perform_view_hpp

#include "widgets/panel_canvas.hpp"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bridge { struct AppContext; }

namespace tzplapp {

class ControlsDispatcher;

class PerformView : public juce::Component {
public:
    PerformView(bridge::AppContext& appCtx, ControlsDispatcher& dispatcher,
                std::function<std::vector<std::string>()> panels,
                std::function<void()> onExit);

    // Rebuild the tab set from the current panel list (call on show).
    void refreshPanels();

    void resized() override;
    void paint(juce::Graphics& g) override;
    bool keyPressed(juce::KeyPress const& key) override;

private:
    void showPanel(int index);

    bridge::AppContext& appCtx_;
    ControlsDispatcher& dispatcher_;
    std::function<std::vector<std::string>()> panels_;
    std::function<void()> onExit_;

    juce::TextButton exitButton_ { "Exit Perform" };
    juce::Label titleLabel_;
    juce::Component tabStrip_;
    std::vector<std::unique_ptr<juce::TextButton>> tabs_;
    juce::Component content_;                     // hosts the scaled canvas
    std::unique_ptr<PanelCanvas> canvas_;
    std::vector<std::string> panelNames_;
    int selected_ = 0;

    static constexpr float kScale = 1.5f;
    static constexpr int kBarH = 30;
    static constexpr int kTabH = 26;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerformView)
};

}

#endif /* perform_view_hpp */
