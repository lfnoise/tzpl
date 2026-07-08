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
//  notebook_view.cpp
//  app (JUCE)
//

#include "notebook_view.hpp"
#include "tzpl_app_context.hpp"
#include "repl_session.hpp"
#include "diagnostic.hpp"
#include <functional>

namespace tzplapp {

using juce::String;
using doc::CellId;
using doc::CellKind;

NotebookView::NotebookView(bridge::AppContext& appCtx, GuiState& guiState,
                           std::function<ts::REPLSession*()> session)
    : appCtx_(appCtx), guiState_(guiState), session_(std::move(session))
{
    addCodeButton_.onClick  = [this] { addCell(CellKind::Code); };
    addProseButton_.onClick = [this] { addCell(CellKind::Prose); };
    addPanelButton_.onClick = [this] { addCell(CellKind::Panel); };
    runAllButton_.onClick   = [this] { runAll(); };
    for (auto* b : { &addCodeButton_, &addProseButton_, &addPanelButton_,
                     &runAllButton_ })
        toolbar_.addAndMakeVisible(b);
    addAndMakeVisible(toolbar_);

    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    juce::Desktop::getInstance().addFocusChangeListener(this);
    newDocument();
}

NotebookView::~NotebookView() {
    juce::Desktop::getInstance().removeFocusChangeListener(this);
}

void NotebookView::resized() {
    auto r = getLocalBounds();
    auto bar = r.removeFromTop(28);
    toolbar_.setBounds(bar);
    bar.reduce(4, 3);
    for (auto* b : { &addCodeButton_, &addProseButton_, &addPanelButton_ }) {
        b->setBounds(bar.removeFromLeft(64));
        bar.removeFromLeft(4);
    }
    runAllButton_.setBounds(bar.removeFromRight(64));
    viewport_.setBounds(r);
    relayoutContent();
}

// ---------------------------------------------------------------------------
// Document lifecycle
// ---------------------------------------------------------------------------

void NotebookView::newDocument() {
    store_.reset(std::make_shared<doc::DocSnapshot const>(), "");
    cells_.clear();
    runQueue_.clear();
    selectedCell_ = 0;
    store_.insertCell(0, CellKind::Code);      // something to type into
    store_.clearModified();
    store_.rerootHistory("new");
    rebuildCells();
    if (!store_.snapshot()->cells.empty())
        selectCell(store_.snapshot()->cells.front()->id);
}

bool NotebookView::openFile(juce::File const& file, String& err) {
    std::string errStr;
    doc::LoadedHistory hist;
    auto snap = doc::loadDocument(appCtx_, file.getFullPathName().toStdString(),
                                  errStr, &store_.interns(), &hist);
    if (!snap) { err = errStr; return false; }
    store_.reset(std::move(snap), file.getFullPathName().toStdString());
    cells_.clear();
    runQueue_.clear();
    selectedCell_ = 0;
    if (hist.root) {
        store_.setWidgetSnap(doc::captureWidgets(
            appCtx_.uiState, claimedPanels(), store_.snapshot()->widgets.get()));
        store_.adoptHistory(std::move(hist.root), hist.cursor);
    } else {
        store_.setWidgetSnap(doc::captureWidgets(
            appCtx_.uiState, claimedPanels(), store_.snapshot()->widgets.get()));
        store_.rerootHistory("open");
    }
    rebuildCells();
    if (!store_.snapshot()->cells.empty())
        selectCell(store_.snapshot()->cells.front()->id);
    queueRunOnLoad();
    return true;
}

bool NotebookView::saveToFile(juce::File const& file, String& err) {
    syncAllCellText();
    std::string errStr;
    if (!doc::saveDocument(store_, appCtx_, file.getFullPathName().toStdString(),
                           errStr)) {
        err = errStr;
        return false;
    }
    store_.setFilePath(file.getFullPathName().toStdString());
    store_.clearModified();
    return true;
}

juce::File NotebookView::currentFile() const {
    auto const& p = store_.filePath();
    return p.empty() ? juce::File() : juce::File(String(p));
}

bool NotebookView::isModified() const {
    if (store_.modified()) return true;
    // An editor whose text drifted from the snapshot counts as modified.
    for (auto const& c : store_.snapshot()->cells) {
        auto it = cells_.find(c->id);
        if (it != cells_.end() && it->second->hasEditor()
            && it->second->editorText() != String(c->text))
            return true;
    }
    return false;
}

std::vector<std::string> NotebookView::claimedPanels() const {
    std::vector<std::string> names;
    for (auto const& c : store_.snapshot()->cells)
        if (c->kind == CellKind::Panel) names.push_back(c->name);
    return names;
}

// ---------------------------------------------------------------------------
// Cell view reconciliation
// ---------------------------------------------------------------------------

void NotebookView::rebuildCells() {
    auto snap = store_.snapshot();

    // Drop views whose cells no longer exist.
    std::unordered_map<CellId, bool> live;
    for (auto const& c : snap->cells) live[c->id] = true;
    for (auto it = cells_.begin(); it != cells_.end();) {
        if (!live.count(it->first)) it = cells_.erase(it);
        else ++it;
    }

    // Create views for new cells and (re)order them under content_.
    content_.removeAllChildren();
    for (auto const& cell : snap->cells) {
        auto& slot = cells_[cell->id];
        if (!slot) {
            slot = std::make_unique<CellComponent>(cell->id, tokeniser_, fontSize_);
            CellId cid = cell->id;
            slot->onRun = [this, cid] { selectCell(cid); launchCell(cid); };
            slot->onDelete = [this, cid] {
                selectCell(cid);
                deleteSelectedCell();
            };
            slot->onMove = [this, cid](int delta) {
                store_.moveCell(cid, delta);
                rebuildCells();
            };
            slot->onCollapse = [this, cid](bool c) {
                store_.setCellCollapsed(cid, c);
                relayoutContent();
            };
            slot->onTextChanged = [this] { relayoutContent(); };
        }
        slot->syncFromModel(*cell);
        slot->setSelected(cell->id == selectedCell_);
        content_.addAndMakeVisible(*slot);
    }
    relayoutContent();
}

void NotebookView::relayoutContent() {
    int width = viewport_.getMaximumVisibleWidth();
    int y = 0;
    for (auto const& cell : store_.snapshot()->cells) {
        auto it = cells_.find(cell->id);
        if (it == cells_.end()) continue;
        int h = it->second->preferredHeight(width);
        it->second->setBounds(0, y, width, h);
        y += h;
    }
    content_.setSize(width, juce::jmax(y, viewport_.getHeight()));
}

CellComponent* NotebookView::cellFor(CellId id) {
    auto it = cells_.find(id);
    return it == cells_.end() ? nullptr : it->second.get();
}

void NotebookView::selectCell(CellId id) {
    if (selectedCell_ == id) return;
    if (auto* prev = cellFor(selectedCell_)) prev->setSelected(false);
    selectedCell_ = id;
    if (auto* cur = cellFor(id)) cur->setSelected(true);
}

void NotebookView::globalFocusChanged(juce::Component* focused) {
    // Select the cell that owns the newly-focused component.
    for (auto* c = focused; c != nullptr; c = c->getParentComponent()) {
        if (auto* cell = dynamic_cast<CellComponent*>(c)) {
            selectCell(cell->cellId());
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Cell operations
// ---------------------------------------------------------------------------

void NotebookView::addCell(CellKind kind) {
    int index = selectedCell_ ? store_.indexOf(selectedCell_) + 1
                              : store_.cellCount();
    CellId id = store_.insertCell(index, kind);
    rebuildCells();
    selectCell(id);
}

void NotebookView::deleteSelectedCell() {
    if (!selectedCell_) return;
    int idx = store_.indexOf(selectedCell_);
    store_.removeCell(selectedCell_);
    cells_.erase(selectedCell_);
    selectedCell_ = 0;
    rebuildCells();
    auto snap = store_.snapshot();
    if (!snap->cells.empty()) {
        int pick = juce::jlimit(0, (int)snap->cells.size() - 1, idx);
        selectCell(snap->cells[pick]->id);
    }
}

void NotebookView::setFontSize(float px) {
    fontSize_ = px;
    for (auto& [id, cell] : cells_) cell->setFontSize(px);
    relayoutContent();
}

// ---------------------------------------------------------------------------
// Eval
// ---------------------------------------------------------------------------

void NotebookView::syncCellTextToModel(CellId id) {
    if (auto* cell = cellFor(id); cell && cell->hasEditor())
        store_.setCellText(id, cell->editorText().toStdString());
}

void NotebookView::syncAllCellText() {
    for (auto const& c : store_.snapshot()->cells) syncCellTextToModel(c->id);
}

void NotebookView::launchCell(CellId id) {
    auto* session = session_();
    if (!session || guiState_.asyncEval.busy()) return;
    auto cell = store_.cell(id);
    if (!cell || cell->kind != CellKind::Code) return;

    syncCellTextToModel(id);
    cell = store_.cell(id);
    if (auto* cc = cellFor(id)) cc->clearOutput();
    guiState_.asyncEval.launch(cell->text, appCtx_, *session, -1, -1, id);
}

void NotebookView::runFocusedCell() {
    if (selectedCell_) launchCell(selectedCell_);
}

void NotebookView::runAll() {
    runQueue_.clear();
    for (auto const& c : store_.snapshot()->cells)
        if (c->kind == CellKind::Code) runQueue_.push_back(c->id);
    pumpRunQueue();
}

void NotebookView::queueRunOnLoad() {
    for (auto const& c : store_.snapshot()->cells)
        if (c->kind == CellKind::Code && c->runOnLoad) runQueue_.push_back(c->id);
    pumpRunQueue();
}

void NotebookView::pumpRunQueue() {
    if (runQueue_.empty() || guiState_.asyncEval.busy()) return;
    CellId id = runQueue_.front();
    runQueue_.pop_front();
    launchCell(id);
}

void NotebookView::onCellEvalDone(CellId cellId,
                                  ts::REPLSession::EvalResult const& result,
                                  std::string const& code) {
    auto cell = store_.cell(cellId);
    auto* cc = cellFor(cellId);
    if (!cell || !cc) return;

    if (!result.errors.empty()) {
        if (!runQueue_.empty()) {
            cc->addOutputLine({ "Run All stopped here -- "
                                    + std::to_string(runQueue_.size())
                                    + " queued cell(s) not run",
                                LineKind::Error });
            runQueue_.clear();
        }
        auto formatted = ts::formatErrorsPlain(result.errors, code, "<cell>");
        for (auto& line : formatted)
            cc->addOutputLine({ line, LineKind::Error });
        std::vector<std::pair<int, String>> markers;
        for (auto const& e : result.errors)
            markers.emplace_back((int)e.loc.start.line - 1, String(e.message));
        cc->setErrorMarkers(markers);
    } else if (result.hasValue) {
        cc->addOutputLine({ "\xe2\x86\x92 " + result.prettyValue
                                + " : " + result.typeName,
                            LineKind::Result });
    }

    // One history node per eval (widgets captured under the AppContext).
    store_.setWidgetSnap(doc::captureWidgets(
        appCtx_.uiState, claimedPanels(), store_.snapshot()->widgets.get()));
    store_.commit("run cell");

    pumpRunQueue();
}

void NotebookView::addCellOutput(CellId cellId, OutputLine const& line) {
    if (auto* cc = cellFor(cellId)) cc->addOutputLine(line);
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Test hooks
// ---------------------------------------------------------------------------

void NotebookView::testTypeIntoFocusedCell(String const& text) {
    if (auto* cc = cellFor(selectedCell_); cc && cc->editor())
        cc->editor()->getDocument().replaceAllContent(text);
}

String NotebookView::testFocusedCellOutput() const {
    auto it = cells_.find(selectedCell_);
    if (it == cells_.end()) return {};
    String out;
    for (auto const& line : it->second->outputLines())
        out << String(line.text) << "\n";
    return out.trim();
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

void NotebookView::undoDocument() {
    auto before = claimedPanels();
    if (auto snap = store_.undo()) {
        doc::restoreWidgets(appCtx_, snap->widgets ? *snap->widgets
                                                   : doc::WidgetSnapList{},
                            before);
        rebuildCells();
    }
}

void NotebookView::redoDocument() {
    auto before = claimedPanels();
    if (auto snap = store_.redo()) {
        doc::restoreWidgets(appCtx_, snap->widgets ? *snap->widgets
                                                   : doc::WidgetSnapList{},
                            before);
        rebuildCells();
    }
}

}
