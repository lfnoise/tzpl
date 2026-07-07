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
//  main_component.hpp
//  app (JUCE)
//
//  The window's content: center pane over output console with a draggable
//  split, and the ApplicationCommandTarget handling every app command.
//  The center/output panes are M1 placeholders -- the code editor (M2) and
//  notebook (M3) replace them without changing the surrounding shell.
//

#ifndef main_component_hpp
#define main_component_hpp

#include "app_commands.hpp"
#include "tzpl_look_and_feel.hpp"
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace bridge { struct AppContext; }

namespace tzplapp {

// Read-only scrolling log; becomes the real output console in M2.
class OutputLog : public juce::Component {
public:
    OutputLog();
    void resized() override { text_.setBounds(getLocalBounds()); }
    void appendLine(juce::String const& line);
    void clear() { text_.clear(); }
    void setFontSize(float px);
    void lookAndFeelChanged() override;

private:
    juce::TextEditor text_;
};

class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget {
public:
    MainComponent(bridge::AppContext& appCtx,
                  juce::ApplicationCommandManager& commands,
                  TzplLookAndFeel& lookAndFeel,
                  juce::PropertiesFile& settings);
    ~MainComponent() override;

    void resized() override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& ids) override;
    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override;
    bool perform(InvocationInfo const& info) override;

    // Append a line to the output console.
    void logLine(juce::String const& line);

    // Asks about unsaved changes (async) and calls `proceed` only if the
    // user did not cancel. With nothing unsaved it proceeds immediately.
    void confirmUnsavedChangesThen(std::function<void()> proceed);

    int currentTheme() const { return lookAndFeel_.currentTheme(); }
    int currentFontIndex() const { return fontIndex_; }

private:
    void applyTheme(int themeIdx);
    void applyFontIndex(int idx);
    void saveSplitRatio();

    bridge::AppContext& appCtx_;
    juce::ApplicationCommandManager& commands_;
    TzplLookAndFeel& lookAndFeel_;
    juce::PropertiesFile& settings_;

    // M1 placeholder for the editor/notebook pane.
    juce::Label centerPane_;
    OutputLog outputLog_;
    juce::StretchableLayoutManager layout_;
    std::unique_ptr<juce::StretchableLayoutResizerBar> resizer_;

    std::unique_ptr<juce::FileChooser> fileChooser_;
    int fontIndex_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

}

#endif /* main_component_hpp */
