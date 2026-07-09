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
//  Console pane: colored, read-only log. Display only -- MainComponent owns
//  the print-drain coordinator (a print can arrive from any thread and may
//  belong to an in-flight notebook cell rather than the console).
//

#ifndef output_console_hpp
#define output_console_hpp

#include "gui_state.hpp"
#include <juce_gui_extra/juce_gui_extra.h>

namespace tzplapp {

class OutputConsole : public juce::Component {
public:
    OutputConsole();

    void resized() override { text_.setBounds(getLocalBounds()); }
    void lookAndFeelChanged() override;

    void appendLine(OutputLine const& line);
    void clear();
    void setFontSize(float px);

private:
    juce::TextEditor text_;
    float fontSize_;
};

}

#endif /* output_console_hpp */
