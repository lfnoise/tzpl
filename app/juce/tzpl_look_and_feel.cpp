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
//  tzpl_look_and_feel.cpp
//  app (JUCE)
//

#include "tzpl_look_and_feel.hpp"

namespace tzplapp {

namespace {

juce::Colour rgb(float r, float g, float b) {
    return juce::Colour::fromFloatRGBA(r, g, b, 1.0f);
}

struct ThemeColors {
    juce::Colour windowBg;   // main window / editor background
    juce::Colour widgetBg;   // frames, text fields, tabs
    juce::Colour menuBg;     // menus, popups, toolbars
    juce::Colour outline;    // borders, separators
    juce::Colour text;
    juce::Colour dimText;    // disabled text; also default fill base
    juce::Colour highlight;  // selection / active highlight fill
    juce::Colour accent;     // check marks, active grabs, focus
};

// Key colors lifted from the corresponding ImGui themes in themes.cpp.
ThemeColors themeColors(int t) {
    switch (t) {
        case themeCorpGray:
            return { rgb(0.25f, 0.25f, 0.25f), rgb(0.42f, 0.42f, 0.42f),
                     rgb(0.335f, 0.335f, 0.335f), rgb(0.12f, 0.12f, 0.12f),
                     rgb(1.0f, 1.0f, 1.0f), rgb(0.40f, 0.40f, 0.40f),
                     rgb(0.47f, 0.47f, 0.47f), rgb(0.26f, 0.59f, 0.98f) };
        case themeDark2:
            return { rgb(0.13f, 0.12f, 0.12f), rgb(0.06f, 0.06f, 0.06f),
                     rgb(0.12f, 0.12f, 0.12f), rgb(0.53f, 0.53f, 0.53f),
                     rgb(1.0f, 1.0f, 1.0f), rgb(0.50f, 0.50f, 0.50f),
                     rgb(0.47f, 0.60f, 0.76f), rgb(0.79f, 0.79f, 0.79f) };
        case themeCherry:
            return { rgb(0.13f, 0.14f, 0.17f), rgb(0.20f, 0.22f, 0.27f),
                     rgb(0.17f, 0.19f, 0.23f), rgb(0.14f, 0.16f, 0.19f),
                     rgb(0.86f, 0.93f, 0.89f), rgb(0.43f, 0.46f, 0.44f),
                     rgb(0.455f, 0.198f, 0.301f), rgb(0.71f, 0.22f, 0.27f) };
        case themeDarkGrey:
            return { rgb(0.06f, 0.06f, 0.06f), rgb(0.20f, 0.21f, 0.22f),
                     rgb(0.14f, 0.14f, 0.14f), rgb(0.43f, 0.43f, 0.50f),
                     rgb(1.0f, 1.0f, 1.0f), rgb(0.50f, 0.50f, 0.50f),
                     rgb(0.70f, 0.70f, 0.70f), rgb(0.94f, 0.94f, 0.94f) };
        case themeDark:
        default:
            // ImGui's stock dark style
            return { rgb(0.10f, 0.10f, 0.10f), rgb(0.16f, 0.19f, 0.28f),
                     rgb(0.14f, 0.14f, 0.14f), rgb(0.43f, 0.43f, 0.50f),
                     rgb(1.0f, 1.0f, 1.0f), rgb(0.50f, 0.50f, 0.50f),
                     rgb(0.26f, 0.59f, 0.98f), rgb(0.26f, 0.59f, 0.98f) };
    }
}

}

void TzplLookAndFeel::applyTheme(int t) {
    theme_ = t;
    ThemeColors c = themeColors(t);

    using UIColour = juce::LookAndFeel_V4::ColourScheme::UIColour;
    juce::LookAndFeel_V4::ColourScheme scheme(
        c.windowBg,           // windowBackground
        c.widgetBg,           // widgetBackground
        c.menuBg,             // menuBackground
        c.outline,            // outline
        c.text,               // defaultText
        c.dimText,            // defaultFill
        c.text,               // highlightedText
        c.highlight,          // highlightedFill
        c.text);              // menuText
    setColourScheme(scheme);

    // Text/code editor colors follow the theme.
    setColour(juce::TextEditor::backgroundColourId, c.windowBg);
    setColour(juce::TextEditor::textColourId, c.text);
    setColour(juce::TextEditor::highlightColourId, c.highlight.withAlpha(0.45f));
    setColour(juce::TextEditor::highlightedTextColourId, c.text);
    setColour(juce::TextEditor::outlineColourId, c.outline.withAlpha(0.5f));
    setColour(juce::TextEditor::focusedOutlineColourId, c.accent.withAlpha(0.7f));
    setColour(juce::CaretComponent::caretColourId, c.text);
    setColour(juce::ScrollBar::thumbColourId, c.dimText);
    setColour(juce::TooltipWindow::backgroundColourId, c.menuBg);
    setColour(juce::TooltipWindow::textColourId, c.text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, c.highlight);
    setColour(juce::ResizableWindow::backgroundColourId, c.windowBg);
    setColour(juce::DocumentWindow::textColourId, c.text);
}

}
