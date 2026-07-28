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
//  sidebar_panel.cpp
//  app (JUCE)
//

#include "sidebar_panel.hpp"
#include "tzpl_fonts.hpp"
#include <algorithm>

namespace tzplapp {

namespace {

// Documents the app opens itself; everything else is dimmed in the tree
// (still openable -- plain text files are fair game for the editor).
bool isDocumentFile(juce::File const& f) {
    return f.hasFileExtension("x") || f.hasFileExtension("tzd");
}

// Guard against loading a binary (a .dylib sitting next to the sources, say)
// into a text editor: reject anything oversized or with a NUL byte up front.
bool looksLikeText(juce::File const& f) {
    if (isDocumentFile(f)) return true;
    if (f.getSize() > 8 * 1024 * 1024) return false;
    juce::FileInputStream in(f);
    if (!in.openedOk()) return false;
    char buf[1024];
    int n = in.read(buf, (int)sizeof(buf));
    for (int i = 0; i < n; ++i)
        if (buf[i] == 0) return false;
    return true;
}

}

// ---------------------------------------------------------------------------
// Tree items
// ---------------------------------------------------------------------------

// One file or directory. Directories list their children the first time they
// are opened and drop them again when closed, so a deep tree costs nothing
// until it is explored.
class SidebarPanel::FileItem : public juce::TreeViewItem {
public:
    FileItem(SidebarPanel& owner, juce::File const& file, bool isRoot)
        : owner_(owner), file_(file), isRoot_(isRoot),
          isDir_(file.isDirectory()) {}

    juce::File const& file() const { return file_; }

    bool mightContainSubItems() override { return isDir_; }
    // Openness state is keyed on this, so it must survive a re-scan.
    juce::String getUniqueName() const override {
        return file_.getFullPathName();
    }
    int getItemHeight() const override { return owner_.rowHeight(); }

    void itemOpennessChanged(bool isNowOpen) override {
        if (isNowOpen) {
            if (getNumSubItems() == 0) buildChildren();
        } else {
            clearSubItems();
        }
    }

    void paintItem(juce::Graphics& g, int width, int height) override {
        auto& lf = owner_.getLookAndFeel();
        if (isSelected()) {
            g.setColour(lf.findColour(juce::TextEditor::highlightColourId));
            g.fillRect(0, 0, width, height);
        }
        auto text = lf.findColour(juce::TextEditor::textColourId);
        // Dim what the app does not open as a document, so .x and .tzd files
        // stand out in a folder full of audio files and build artefacts.
        if (!isDir_ && !isDocumentFile(file_))
            text = text.withMultipliedAlpha(0.55f);
        g.setColour(text);
        g.setFont(owner_.rowFont(isRoot_));
        g.drawText(file_.getFileName(), 2, 0, width - 4, height,
                   juce::Justification::centredLeft, true);
    }

    void itemClicked(juce::MouseEvent const& e) override {
        if (e.mods.isPopupMenu()) {
            showContextMenu();
            return;
        }
        if (isDir_) setOpen(!isOpen());
        else owner_.openFile(file_);
    }

    // Re-scan open directories whose contents changed on disk. Returns true
    // if anything was rebuilt.
    bool refreshChanged(bool force) {
        if (!isDir_ || !isOpen()) return false;
        bool changed = false;
        if (force || file_.getLastModificationTime() != listedAt_) {
            auto state = getOpennessState();
            clearSubItems();
            buildChildren();
            if (state != nullptr) restoreOpennessState(*state);
            changed = true;
        }
        for (int i = 0; i < getNumSubItems(); ++i)
            if (auto* child = dynamic_cast<FileItem*>(getSubItem(i)))
                changed |= child->refreshChanged(force);
        return changed;
    }

private:
    void buildChildren() {
        listedAt_ = file_.getLastModificationTime();
        auto entries = file_.findChildFiles(
            juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles,
            false);
        // Directories first, then files, each case-insensitively by name.
        std::sort(entries.begin(), entries.end(),
                  [](juce::File const& a, juce::File const& b) {
                      bool ad = a.isDirectory(), bd = b.isDirectory();
                      if (ad != bd) return ad;
                      return a.getFileName().compareIgnoreCase(b.getFileName())
                             < 0;
                  });
        for (auto& e : entries)
            addSubItem(new FileItem(owner_, e, /*isRoot=*/false));
    }

    void showContextMenu() {
        juce::PopupMenu m;
        enum { revealID = 1, refreshID, closeID };
        m.addItem(revealID, "Reveal in Finder");
        if (isDir_) m.addItem(refreshID, "Refresh");
        if (isRoot_) {
            m.addSeparator();
            m.addItem(closeID, "Close Folder");
        }
        // The callback lands after this click returns, by which time the item
        // may be gone: capture the panel (SafePointer) and the path, never
        // `this`, and look the item up again on the other side.
        juce::Component::SafePointer<SidebarPanel> safe(&owner_);
        juce::File file = file_;
        m.showMenuAsync(juce::PopupMenu::Options(),
                        [safe, file](int result) {
            if (safe == nullptr || result == 0) return;
            switch (result) {
            case revealID:  file.revealToUser();     break;
            case refreshID: safe->refreshItemAt(file); break;
            case closeID:   safe->removeFolder(file); break;
            }
        });
    }

    SidebarPanel& owner_;
    juce::File file_;
    bool isRoot_ = false;
    bool isDir_ = false;
    // Modification time of the directory when its children were listed.
    juce::Time listedAt_;
};

// The invisible item holding one FileItem per opened folder.
class SidebarPanel::RootHolder : public juce::TreeViewItem {
public:
    bool mightContainSubItems() override { return true; }
};

// TreeView with Return opening the selected file (arrow keys and left/right
// expansion are already handled by TreeView itself).
class SidebarPanel::FileTree : public juce::TreeView {
public:
    explicit FileTree(SidebarPanel& owner) : owner_(owner) {}

    bool keyPressed(juce::KeyPress const& key) override {
        if (key == juce::KeyPress::returnKey)
            if (auto* item = dynamic_cast<FileItem*>(getSelectedItem(0)))
                if (item->file().existsAsFile()) {
                    owner_.openFile(item->file());
                    return true;
                }
        return juce::TreeView::keyPressed(key);
    }

private:
    SidebarPanel& owner_;
};

// ---------------------------------------------------------------------------
// SidebarPanel
// ---------------------------------------------------------------------------

SidebarPanel::SidebarPanel() {
    tree_ = std::make_unique<FileTree>(*this);
    root_ = std::make_unique<RootHolder>();
    tree_->setRootItem(root_.get());
    tree_->setRootItemVisible(false);
    tree_->setDefaultOpenness(false);
    tree_->setMultiSelectEnabled(false);
    tree_->setOpenCloseButtonsVisible(true);
    addAndMakeVisible(*tree_);
    rebuildFonts();
    lookAndFeelChanged();
}

SidebarPanel::~SidebarPanel() {
    // The TreeView must not outlive the items it points at.
    tree_->setRootItem(nullptr);
}

void SidebarPanel::resized() {
    tree_->setBounds(getLocalBounds().reduced(2));
}

void SidebarPanel::paint(juce::Graphics& g) {
    auto& lf = getLookAndFeel();
    g.fillAll(lf.findColour(juce::ResizableWindow::backgroundColourId));
    if (hasFolders()) return;
    g.setColour(lf.findColour(juce::TextEditor::textColourId)
                    .withMultipliedAlpha(0.6f));
    g.setFont(juce::Font(juce::FontOptions(fontSize_)));
    g.drawFittedText("No folder open\n\nFile > Open Folder...",
                     getLocalBounds().reduced(10),
                     juce::Justification::centred, 4);
}

void SidebarPanel::lookAndFeelChanged() {
    auto& lf = getLookAndFeel();
    tree_->setColour(juce::TreeView::backgroundColourId,
                     lf.findColour(juce::ResizableWindow::backgroundColourId));
    tree_->setColour(juce::TreeView::linesColourId,
                     lf.findColour(juce::TextEditor::textColourId)
                         .withMultipliedAlpha(0.25f));
    repaint();
}

void SidebarPanel::rebuildFonts() {
    fileFont_ = juce::Font(
        juce::FontOptions(monoFontName(), fontSize_, juce::Font::plain));
    rootFont_ = juce::Font(
        juce::FontOptions(monoFontName(), fontSize_, juce::Font::bold));
}

void SidebarPanel::setFontSize(float px) {
    fontSize_ = px;
    rebuildFonts();
    // Row heights come from getItemHeight(), which the tree caches.
    root_->treeHasChanged();
    repaint();
}

int SidebarPanel::rowHeight() const {
    return juce::roundToInt(fontSize_ * 1.5f);
}

SidebarPanel::FileItem* SidebarPanel::folderItem(juce::File const& dir) const {
    for (int i = 0; i < root_->getNumSubItems(); ++i)
        if (auto* item = dynamic_cast<FileItem*>(root_->getSubItem(i)))
            if (item->file() == dir) return item;
    return nullptr;
}

SidebarPanel::FileItem* SidebarPanel::findItem(juce::File const& path) const {
    // Depth-first over the materialised items (closed directories have none).
    std::function<FileItem*(juce::TreeViewItem*)> walk =
        [&](juce::TreeViewItem* parent) -> FileItem* {
            for (int i = 0; i < parent->getNumSubItems(); ++i) {
                auto* sub = parent->getSubItem(i);
                if (auto* item = dynamic_cast<FileItem*>(sub))
                    if (item->file() == path) return item;
                if (auto* found = walk(sub)) return found;
            }
            return nullptr;
        };
    return walk(root_.get());
}

void SidebarPanel::refreshItemAt(juce::File const& path) {
    if (auto* item = findItem(path)) item->refreshChanged(true);
}

void SidebarPanel::addFolder(juce::File const& dir) {
    if (!dir.isDirectory()) {
        if (onMessage) onMessage("not a folder: " + dir.getFullPathName());
        return;
    }
    if (auto* existing = folderItem(dir)) {
        existing->setSelected(true, true);
        tree_->scrollToKeepItemVisible(existing);
        return;
    }
    auto* item = new FileItem(*this, dir, /*isRoot=*/true);
    root_->addSubItem(item);
    item->setOpen(true);
    repaint();
    if (onFoldersChanged) onFoldersChanged();
}

void SidebarPanel::removeFolder(juce::File const& dir) {
    if (auto* item = folderItem(dir)) {
        root_->removeSubItem(item->getIndexInParent());
        repaint();
        if (onFoldersChanged) onFoldersChanged();
    }
}

bool SidebarPanel::hasFolders() const {
    return root_ != nullptr && root_->getNumSubItems() > 0;
}

juce::StringArray SidebarPanel::folderPaths() const {
    juce::StringArray paths;
    for (int i = 0; i < root_->getNumSubItems(); ++i)
        if (auto* item = dynamic_cast<FileItem*>(root_->getSubItem(i)))
            paths.add(item->file().getFullPathName());
    return paths;
}

void SidebarPanel::setFolderPaths(juce::StringArray const& paths) {
    root_->clearSubItems();
    for (auto& p : paths) {
        juce::File dir(p);
        // Folders that moved or vanished since the last launch are dropped.
        if (!dir.isDirectory()) continue;
        auto* item = new FileItem(*this, dir, /*isRoot=*/true);
        root_->addSubItem(item);
        item->setOpen(true);
    }
    repaint();
}

void SidebarPanel::refreshChangedFolders() {
    for (int i = 0; i < root_->getNumSubItems(); ++i)
        if (auto* item = dynamic_cast<FileItem*>(root_->getSubItem(i)))
            item->refreshChanged(false);
}

void SidebarPanel::refreshAll() {
    for (int i = 0; i < root_->getNumSubItems(); ++i)
        if (auto* item = dynamic_cast<FileItem*>(root_->getSubItem(i)))
            item->refreshChanged(true);
}

int SidebarPanel::testRootCount() const {
    return root_->getNumSubItems();
}

int SidebarPanel::testRowCount() const {
    return tree_->getNumRowsInTree();
}

juce::File SidebarPanel::testClickFirstDocument() {
    if (root_->getNumSubItems() == 0) return {};
    auto* first = root_->getSubItem(0);
    for (int i = 0; i < first->getNumSubItems(); ++i)
        if (auto* item = dynamic_cast<FileItem*>(first->getSubItem(i)))
            if (isDocumentFile(item->file())) {
                openFile(item->file());
                return item->file();
            }
    return {};
}

void SidebarPanel::openFile(juce::File const& file) {
    if (!file.existsAsFile()) {
        if (onMessage) onMessage("no such file: " + file.getFullPathName());
        return;
    }
    if (!looksLikeText(file)) {
        if (onMessage) onMessage("not a text file: " + file.getFileName());
        return;
    }
    if (onOpenFile) onOpenFile(file);
}

}
