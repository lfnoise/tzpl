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
#include "tzpl_fonts.hpp"
#include <juce_gui_extra/juce_gui_extra.h> // CodeEditorComponent colour IDs

namespace tzplapp {

// Space either side of a tab label, so filenames do not run together.
static constexpr int kTabPadding = 14;

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

    setColour(juce::CodeEditorComponent::backgroundColourId, c.windowBg);
    setColour(juce::CodeEditorComponent::defaultTextColourId, c.text);
    setColour(juce::CodeEditorComponent::highlightColourId,
              c.highlight.withAlpha(0.4f));
    setColour(juce::CodeEditorComponent::lineNumberBackgroundId,
              c.windowBg.darker(0.15f));
    setColour(juce::CodeEditorComponent::lineNumberTextId,
              c.dimText);

    // Editor tabs. The front tab gets full-strength text over the editor
    // background; the rest are dimmed and sit on a slightly offset fill.
    tabBg_ = c.windowBg;
    tabOutline_ = c.outline;
    tabAccent_ = c.accent;
    setColour(juce::TabbedComponent::backgroundColourId, c.windowBg);
    setColour(juce::TabbedComponent::outlineColourId, c.outline.withAlpha(0.5f));
    setColour(juce::TabbedButtonBar::tabOutlineColourId,
              c.outline.withAlpha(0.4f));
    setColour(juce::TabbedButtonBar::frontOutlineColourId, c.accent);
    setColour(juce::TabbedButtonBar::tabTextColourId,
              c.text.withMultipliedAlpha(0.6f));
    setColour(juce::TabbedButtonBar::frontTextColourId, c.text);
}

void TzplLookAndFeel::drawTabButton(juce::TabBarButton& button,
                                    juce::Graphics& g,
                                    bool isMouseOver, bool isMouseDown) {
    auto const orientation = button.getTabbedButtonBar().getOrientation();
    bool const front = button.getToggleState();

    // contrasting() moves away from the fill in whichever direction the theme
    // allows, so this reads on both the near-black and the grey themes.
    auto fill = front ? tabBg_
              : (isMouseOver || isMouseDown) ? tabBg_.contrasting(0.10f)
                                             : tabBg_.contrasting(0.05f);
    auto area = button.getActiveArea();
    g.setColour(fill);
    g.fillRect(area);

    // Hairline between neighbouring tabs.
    auto edge = area;
    g.setColour(tabOutline_.withAlpha(0.4f));
    if (button.getTabbedButtonBar().isVertical())
        g.fillRect(edge.removeFromBottom(1));
    else
        g.fillRect(edge.removeFromRight(1));

    // The accent bar on the outer edge is what actually says "this one".
    if (front) {
        auto bar = area;
        g.setColour(tabAccent_);
        switch (orientation) {
        case juce::TabbedButtonBar::TabsAtTop:
            g.fillRect(bar.removeFromTop(2));    break;
        case juce::TabbedButtonBar::TabsAtBottom:
            g.fillRect(bar.removeFromBottom(2)); break;
        case juce::TabbedButtonBar::TabsAtLeft:
            g.fillRect(bar.removeFromLeft(2));   break;
        case juce::TabbedButtonBar::TabsAtRight:
            g.fillRect(bar.removeFromRight(2));  break;
        }
    }

    drawTabButtonText(button, g, isMouseOver, isMouseDown);
}

juce::Font TzplLookAndFeel::getTabButtonFont(juce::TabBarButton&, float height) {
    return juce::Font(monoFont(juce::jmin(13.0f, height * 0.5f)));
}

int TzplLookAndFeel::getTabButtonBestWidth(juce::TabBarButton& button,
                                           int tabDepth) {
    auto font = getTabButtonFont(button, (float)tabDepth);
    int width = juce::GlyphArrangement::getStringWidthInt(
                    font, button.getButtonText().trim())
              + kTabPadding * 2;

    if (auto* extra = button.getExtraComponent())
        width += button.getTabbedButtonBar().isVertical() ? extra->getHeight()
                                                          : extra->getWidth();
    return juce::jlimit(tabDepth * 2, tabDepth * 10, width);
}

}
