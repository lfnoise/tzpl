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
//  search_panel.hpp
//  app (JUCE)
//
//  The sidebar's second face: Find in Files and Find Definitions results,
//  the way Xcode's navigator swaps its file tree for a find navigator.
//  A search field with the five-mode pattern popup and case toggle sits
//  over a tree of files, each expanding to its matching lines. The panel
//  runs no search itself: the owner (MainComponent) supplies the results,
//  since only it knows the open tabs' unsaved text and the module paths.
//

#ifndef search_panel_hpp
#define search_panel_hpp

#include "text_search.hpp"
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <memory>
#include <vector>

namespace tzplapp {

// One matching line (or definition) in a document.
struct SearchHit {
    int line = 0;          // 0-based
    int start = 0;         // document offset of the match
    int length = 0;
    juce::String lineText; // the line, leading whitespace stripped
    int hlStart = 0;       // match within lineText
    int hlLength = 0;
    juce::String tag;      // shown before the line ("fn", "let", ...)
};

struct SearchFileResult {
    juce::String title;     // file name / tab name
    juce::String subtitle;  // where it lives (dimmed)
    juce::File file;        // invalid: an untitled editor tab
    juce::String tabName;   // for untitled tabs
    std::vector<SearchHit> hits;
};

class SearchPanel : public juce::Component {
public:
    SearchPanel();
    ~SearchPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;
    bool keyPressed(juce::KeyPress const& key) override;

    // Focus the field; a non-empty `seed` replaces the term.
    void focusField(juce::String const& seed);
    juce::String term() const { return field_.getText(); }
    MatchMode mode() const;
    bool caseSensitive() const { return caseButton_.getToggleState(); }
    void setTerm(juce::String const& t);

    // Replace the results. `heading` is the summary line ("12 results in
    // 3 files", "Definitions of foo").
    void setResults(juce::String const& heading,
                    std::vector<SearchFileResult> results);
    void clearResults();
    bool hasResults() const { return !results_.empty(); }
    juce::String heading() const { return heading_.getText(); }
    int hitCount() const;

    void setFontSize(float px);
    float fontSize() const { return fontSize_; }
    int rowHeight() const;
    juce::Font const& rowFont(bool bold) const {
        return bold ? boldFont_ : plainFont_;
    }

    // Return in the field, or a mode/case change with a term present.
    std::function<void()> onSearch;
    // A result row was activated (click or Return).
    std::function<void(SearchFileResult const&, SearchHit const&)> onOpenHit;
    // Escape: the owner switches the sidebar back to the file tree.
    std::function<void()> onDismiss;

    // Test hooks.
    bool testClickFirstHit();

private:
    class FileItem;
    class HitItem;
    class RootItem;
    class ResultsTree;

    void rebuildTree();
    void rebuildFonts();
    void openHit(int fileIndex, int hitIndex);

    juce::TextEditor field_;
    juce::ComboBox modeBox_;
    juce::ToggleButton caseButton_ { "Aa" };
    juce::Label heading_;
    std::unique_ptr<ResultsTree> tree_;
    std::unique_ptr<RootItem> root_;
    std::vector<SearchFileResult> results_;
    float fontSize_ = 14.0f;
    juce::Font plainFont_ { juce::FontOptions(14.0f) };
    juce::Font boldFont_ { juce::FontOptions(14.0f) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SearchPanel)
};

}

#endif /* search_panel_hpp */
