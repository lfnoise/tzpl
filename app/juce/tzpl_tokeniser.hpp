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
//  tzpl_tokeniser.hpp
//  app (JUCE)
//
//  Tzopilotl syntax highlighting for juce::CodeEditorComponent. The JUCE
//  counterpart of EditorPanel::createTzopilotlDef() (editor_panel.cpp).
//

#ifndef tzpl_tokeniser_hpp
#define tzpl_tokeniser_hpp

#include <juce_gui_extra/juce_gui_extra.h>

namespace tzplapp {

class TzplTokeniser : public juce::CodeTokeniser {
public:
    enum TokenType {
        tokenError = 0,
        tokenComment,
        tokenKeyword,
        tokenIdentifier,
        tokenNumber,
        tokenString,
        tokenSymbol,      // 'identifier literals
        tokenOperator,
        tokenBracket,
    };

    int readNextToken(juce::CodeDocument::Iterator& source) override;
    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override;
};

}

#endif /* tzpl_tokeniser_hpp */
