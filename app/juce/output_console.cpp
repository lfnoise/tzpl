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
//  output_console.cpp
//  app (JUCE)
//

#include "output_console.hpp"
#include "app_commands.hpp"

namespace tzplapp {

OutputConsole::OutputConsole() : fontSize_(cmd::kEditorFontSizes[0]) {
    text_.setMultiLine(true);
    text_.setReadOnly(true);
    text_.setScrollbarsShown(true);
    text_.setCaretVisible(false);
    text_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                    fontSize_, juce::Font::plain));
    addAndMakeVisible(text_);
}

void OutputConsole::appendLine(OutputLine const& line) {
    juce::Colour c;
    switch (line.kind) {
        case LineKind::Result:    c = juce::Colour(0xff7fd07f); break;
        case LineKind::Error:     c = juce::Colour(0xffff6b6b); break;
        case LineKind::Info:      c = juce::Colour(0xff9fb8d0); break;
        case LineKind::Separator: c = juce::Colour(0xff606060); break;
        case LineKind::Output:
        default:                  c = findColour(juce::TextEditor::textColourId);
    }
    text_.moveCaretToEnd();
    text_.setColour(juce::TextEditor::textColourId, c);
    if (line.kind == LineKind::Separator)
        text_.insertTextAtCaret(juce::String::repeatedString("-", 40) + "\n");
    else
        text_.insertTextAtCaret(juce::String(line.text) + "\n");
}

void OutputConsole::clear() {
    text_.clear();
}

void OutputConsole::setFontSize(float px) {
    fontSize_ = px;
    text_.applyFontToAllText(
        juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), px,
                          juce::Font::plain));
}

void OutputConsole::lookAndFeelChanged() {
    text_.setColour(juce::TextEditor::backgroundColourId,
                    findColour(juce::TextEditor::backgroundColourId));
    text_.repaint();
}

}
