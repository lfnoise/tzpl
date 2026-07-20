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
//  editor_pane.hpp
//  app (JUCE)
//
//  Multi-tab code editor: the JUCE counterpart of editor_panel.{hpp,cpp}.
//  Each tab is a CodeDocument + TzplCodeEditor sharing one TzplTokeniser.
//  Dialog flows (save-as paths, close confirmation) live in MainComponent;
//  this class only exposes synchronous operations.
//

#ifndef editor_pane_hpp
#define editor_pane_hpp

#include "tzpl_tokeniser.hpp"
#include "find_replace_bar.hpp"
#include <juce_gui_extra/juce_gui_extra.h>
#include <map>
#include <memory>
#include <vector>

namespace tzplapp {

// CodeEditorComponent with an overlay for the eval flash and error markers,
// plus typing-burst undo chunking (the JUCE replacement for the vendored
// ImGuiColorTextEdit undo-chunking patch).
class TzplCodeEditor : public juce::CodeEditorComponent {
public:
    TzplCodeEditor(juce::CodeDocument& doc, juce::CodeTokeniser* tokeniser);
    ~TzplCodeEditor() override;

    bool keyPressed(juce::KeyPress const& key) override;
    void resized() override;
    void editorViewportPositionChanged() override;

    // Brief highlight of the just-evaluated lines (0-based, inclusive).
    void triggerFlash(int startLine, int endLine);

    // Error markers: 0-based line -> message. Painted as a red band.
    void setErrorMarkers(std::map<int, juce::String> markers);
    void clearErrorMarkers();

private:
    class Overlay;
    std::unique_ptr<Overlay> overlay_;
    juce::uint32 lastEditMs_ = 0;
};

class EditorPane : public juce::Component,
                   private juce::CodeDocument::Listener {
public:
    EditorPane();
    ~EditorPane() override;

    void resized() override;

    // -- Tabs --
    void newTab(juce::String const& name = "untitled.x");
    // Opens `file`; if already open just selects its tab. False on read error.
    bool openFile(juce::File const& file);
    // Open `file`'s contents in a new tab with no file path (an untitled
    // copy, marked modified): saving prompts for a location. Used for
    // distribution examples, which are templates.
    bool openFileAsCopy(juce::File const& file);
    void closeTab(int index);            // unconditional
    int tabCount() const { return (int)tabs_.size(); }
    int activeTabIndex() const;

    juce::String tabName(int index) const;
    bool tabModified(int index) const;
    bool tabHasFilePath(int index) const;
    juce::File tabFile(int index) const;  // invalid File if no path yet

    bool activeHasFilePath() const { return tabHasFilePath(activeTabIndex()); }
    juce::String activeTabName() const { return tabName(activeTabIndex()); }
    juce::File activeFile() const { return tabFile(activeTabIndex()); }

    // -- Revert / external-change detection --
    // Reload a tab's content from its file on disk, discarding in-memory
    // edits and clearing the external-change flag. False if the tab has no
    // path or the file can no longer be read.
    bool reloadTab(int index);
    bool reloadActive() { return reloadTab(activeTabIndex()); }

    // True once checkExternalChanges has seen the tab's file change on disk
    // since it was last loaded or saved (until the tab is reverted or saved).
    bool tabExternallyChanged(int index) const;
    bool activeExternallyChanged() const {
        return tabExternallyChanged(activeTabIndex());
    }

    // Poll every file-backed tab's on-disk modification time; flag tabs whose
    // file changed under us and repaint their titles. Returns true if any
    // tab's external-change flag flipped (so the menu can refresh).
    bool checkExternalChanges();

    // -- Saving (returns true on success) --
    bool saveTab(int index);                        // to its own path
    bool saveTabAs(int index, juce::File const& f); // adopts the new path
    bool saveActive() { return saveTab(activeTabIndex()); }
    bool saveActiveAs(juce::File const& f) { return saveTabAs(activeTabIndex(), f); }
    bool saveCopy(juce::File const& f) const;       // active tab, path unchanged
    int saveAll();                                  // modified tabs with paths
    bool hasUnsavedChanges() const;
    std::vector<juce::String> unsavedFileNames() const;

    // -- Text access for evaluation --
    juce::String getSelectedText() const;
    juce::String getCurrentLineText() const;
    juce::String getAllText() const;
    // Contiguous non-empty lines around the cursor (0-based, inclusive).
    juce::String getCurrentBlockText(int& outStartLine, int& outEndLine) const;
    int cursorLine() const;

    // -- Edit operations on the active editor --
    void undo();
    void redo();
    void cutToClipboard();
    void copyToClipboard();
    void pasteFromClipboard();
    void selectAll();
    void toggleComment();
    void indentSelection();
    void outdentSelection();

    // -- Eval feedback --
    void triggerFlash(int startLine, int endLine);
    void setErrorMarkersFromEval(
        std::vector<std::pair<int, juce::String>> const& lineMessages);
    void clearErrorMarkers();

    TzplCodeEditor* activeEditor() const;

    void setFontSize(float px);

    // -- Find/Replace --
    void showFind(juce::String const& seed = {});
    void findNext();
    void findPrevious();
    void seedReplace(juce::String const& text);

private:
    struct Tab {
        juce::String name;
        juce::File file;                   // invalid = no path yet
        std::unique_ptr<juce::CodeDocument> doc;
        std::unique_ptr<TzplCodeEditor> editor;
        // On-disk modification time (ms) when this tab was last loaded or
        // saved; 0 for an untitled tab. Compared against the live mtime to
        // detect edits made outside the app.
        juce::int64 diskModTime = 0;
        bool externallyChanged = false;    // disk copy changed under us
    };

    Tab* activeTab() const;
    Tab* tabAt(int index) const;
    int indexOfDocument(juce::CodeDocument const* doc) const;
    void refreshTabTitle(int index);
    void addTabInternal(std::unique_ptr<Tab> tab);
    // Lines covered by the selection (or the caret line), 0-based inclusive.
    bool selectedLineRange(int& first, int& last) const;

    void codeDocumentTextInserted(juce::String const&, int) override;
    void codeDocumentTextDeleted(int, int) override;

    TzplTokeniser tokeniser_;
    juce::TabbedComponent tabsUI_ { juce::TabbedButtonBar::TabsAtTop };
    FindReplaceBar findBar_ { [this] { return activeEditor(); } };
    std::vector<std::unique_ptr<Tab>> tabs_;
    float fontSize_ = 14.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorPane)
};

}

#endif /* editor_pane_hpp */
