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
//  markdown_view.hpp
//  app (JUCE)
//
//  Read-only rendered Markdown for notebook prose cells. Parses with the
//  vendored md4c (GitHub dialect: tables, strikethrough) into a flat block
//  model, then lays blocks out with TextLayout at a given width. Height is
//  a pure function of (text, font size, width) -- measured independently,
//  never read back from any child component (see the feedback-loop note in
//  cell_component.cpp). Double-click asks the host cell to switch to the
//  plain-text editor.
//

#ifndef markdown_view_hpp
#define markdown_view_hpp

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

namespace tzplapp {

class MarkdownView : public juce::Component {
public:
    MarkdownView();
    ~MarkdownView() override;

    void setMarkdown(juce::String const& text);
    void setFontSize(float px);

    // Total rendered height at `width`, including vertical padding.
    int heightForWidth(int width) const;

    void paint(juce::Graphics& g) override;
    void mouseDoubleClick(juce::MouseEvent const&) override {
        if (onEditRequested) onEditRequested();
    }
    void lookAndFeelChanged() override;

    std::function<void()> onEditRequested;

    // Parsed-but-unstyled text of the whole document (test hook).
    juce::String plainText() const;

    struct Model;                       // parsed blocks (width-independent)

private:
    struct Layout;                      // laid-out blocks for one width
    void ensureLayout(int width) const;

    std::unique_ptr<Model> model_;
    mutable std::unique_ptr<Layout> layout_;   // cache for the last width
    float fontSize_ = 13.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkdownView)
};

}

#endif /* markdown_view_hpp */
