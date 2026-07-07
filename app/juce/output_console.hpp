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
//  output_console.hpp
//  app (JUCE)
//
//  Console pane: colored, read-only log of VM prints, eval results, and
//  errors. Drains the shared GuiState buffers (PrintCapture pipe and
//  OutputBuffer) on a low-frequency timer -- prints can arrive from any
//  thread (scheduler, actors, engine) at any time.
//

#ifndef output_console_hpp
#define output_console_hpp

#include "gui_state.hpp"
#include <juce_gui_extra/juce_gui_extra.h>

namespace tzplapp {

class OutputConsole : public juce::Component, private juce::Timer {
public:
    explicit OutputConsole(GuiState& guiState);

    void resized() override { text_.setBounds(getLocalBounds()); }
    void lookAndFeelChanged() override;

    // Drain both buffers now (called on eval completion so results appear
    // immediately rather than on the next timer tick).
    void drainNow();

    void clear();
    void setFontSize(float px);

private:
    void timerCallback() override { drainNow(); }
    void appendLine(OutputLine const& line);

    GuiState& guiState_;
    juce::TextEditor text_;
    float fontSize_;
};

}

#endif /* output_console_hpp */
