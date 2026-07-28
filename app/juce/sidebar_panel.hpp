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
//  sidebar_panel.hpp
//  app (JUCE)
//
//  Workspace sidebar: one lazily-populated file tree per opened folder --
//  the JUCE counterpart of workspace_panel.{hpp,cpp}. Clicking a file asks
//  the owner to open it; directories expand in place. Children are scanned
//  when a directory is first opened and re-scanned when its on-disk
//  modification time changes (see refreshChangedFolders).
//

#ifndef sidebar_panel_hpp
#define sidebar_panel_hpp

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace tzplapp {

class SidebarPanel : public juce::Component {
public:
    SidebarPanel();
    ~SidebarPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;

    // Add `dir` as a tree root. An already-open root is just selected.
    void addFolder(juce::File const& dir);
    void removeFolder(juce::File const& dir);
    bool hasFolders() const;

    // Root paths in display order -- the persisted form.
    juce::StringArray folderPaths() const;
    void setFolderPaths(juce::StringArray const& paths);

    // Re-scan every open directory whose modification time changed since it
    // was listed (files created/deleted/renamed outside the app). Openness,
    // selection and scroll position survive. Cheap enough to poll ~1 Hz.
    void refreshChangedFolders();
    // Unconditional re-scan of every open directory.
    void refreshAll();

    // Row text scales with the editor font so the sidebar matches it.
    void setFontSize(float px);
    float fontSize() const { return fontSize_; }
    int rowHeight() const;

    // A file row was activated (click or Return).
    std::function<void(juce::File)> onOpenFile;
    // A root was added or removed (the owner persists folderPaths()).
    std::function<void()> onFoldersChanged;
    // Status/error text for the console.
    std::function<void(juce::String)> onMessage;

    // Test hooks (TZPL_JUCE_DEMO=sidebar:<dir>).
    int testRootCount() const;
    int testRowCount() const;
    // Activate the first .x/.tzd row under the first root, as a click would.
    // Returns the file opened, or an invalid File if the tree has none.
    juce::File testClickFirstDocument();

private:
    class FileItem;
    class RootHolder;
    class FileTree;

    // The root item for `dir`, or nullptr.
    FileItem* folderItem(juce::File const& dir) const;
    // Any item (root or nested) whose path is `path`, searching only the
    // parts of the tree that are currently open. Used by the context menu,
    // which must not hold a pointer to an item across an async callback.
    FileItem* findItem(juce::File const& path) const;
    void refreshItemAt(juce::File const& path);
    void openFile(juce::File const& file);

    std::unique_ptr<FileTree> tree_;
    std::unique_ptr<RootHolder> root_;   // hidden holder for the roots
    float fontSize_ = 14.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebarPanel)
};

}

#endif /* sidebar_panel_hpp */
