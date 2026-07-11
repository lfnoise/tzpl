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
#include "history_window.hpp"
#include "../widgets/controls_dispatch.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_ui_state.hpp"
#include "repl_session.hpp"
#include "diagnostic.hpp"
#include <functional>

namespace tzplapp {

using juce::String;
using doc::CellId;
using doc::CellKind;

NotebookView::NotebookView(bridge::AppContext& appCtx, GuiState& guiState,
                           std::function<ts::REPLSession*()> session,
                           ControlsDispatcher& dispatcher)
    : appCtx_(appCtx), guiState_(guiState), session_(std::move(session)),
      dispatcher_(dispatcher)
{
    addCodeButton_.onClick  = [this] { addCell(CellKind::Code); };
    addProseButton_.onClick = [this] { addCell(CellKind::Prose); };
    addPanelButton_.onClick = [this] { addCell(CellKind::Panel); };
    addPresetsButton_.onClick = [this] { addCell(CellKind::Presets); };
    historyButton_.onClick  = [this] { toggleHistoryWindow(); };
    runAllButton_.onClick   = [this] { runAll(); };
    for (auto* b : { &addCodeButton_, &addProseButton_, &addPanelButton_,
                     &addPresetsButton_, &historyButton_, &runAllButton_ })
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
    for (auto* b : { &addCodeButton_, &addProseButton_, &addPanelButton_,
                     &addPresetsButton_ }) {
        b->setBounds(bar.removeFromLeft(64));
        bar.removeFromLeft(4);
    }
    runAllButton_.setBounds(bar.removeFromRight(64));
    bar.removeFromRight(4);
    historyButton_.setBounds(bar.removeFromRight(64));
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

void NotebookView::detachFile() {
    store_.setFilePath("");
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

// Reconcile the cell views against the store. Editors are re-seeded from
// the store's text, so THE STORE IS THE TRUTH here: a caller that mutates
// structure while a cell editor holds text typed since the last eval/save
// must syncAllCellText() first, or that text is silently discarded. The
// callers where the store is already the truth (open, new, undo, redo,
// history jump) must NOT sync -- that would push stale editor text back
// over what they just restored.
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
            slot = std::make_unique<CellComponent>(
                cell->id, tokeniser_, fontSize_, appCtx_.uiState, &dispatcher_);
            CellId cid = cell->id;
            slot->onRun = [this, cid] { selectCell(cid); launchCell(cid); };
            slot->onDelete = [this, cid] {
                selectCell(cid);
                deleteSelectedCell();
            };
            slot->onMove = [this, cid](int delta) {
                syncAllCellText();   // rebuildCells() re-seeds every editor
                store_.moveCell(cid, delta);
                rebuildCells();
            };
            slot->onCollapse = [this, cid](bool c) {
                store_.setCellCollapsed(cid, c);
                relayoutContent();
            };
            slot->onTextChanged = [this] { relayoutContent(); };

            // Editable cell name (Code label / Panel target name).
            slot->onRenameCell = [this, cid](juce::String name) {
                auto cell = store_.cell(cid);
                if (!cell || cell->name == name.toStdString()) return;
                syncAllCellText();   // rebuildCells() re-seeds every editor
                store_.setCellName(cid, name.toStdString());
                rebuildCells();  // repoints a Panel cell's canvas
            };

            // Arrange: a widget was moved/resized -- commit the new frames.
            slot->onArrangeCommit = [this] {
                store_.setWidgetSnap(doc::captureWidgets(
                    appCtx_.uiState, claimedPanels(),
                    store_.snapshot()->widgets.get()));
                store_.commit("arrange");
            };

            // Presets: capture/recall operate on the live widget registry
            // and commit a history node (recall changes control values).
            slot->onPresetStore = [this, cid] {
                auto presets = store_.cell(cid)->presets;
                presets.push_back(std::make_shared<doc::Preset const>(
                    capturePreset(presetScope(cid))));
                commitPresets(cid, std::move(presets), "store preset");
            };
            slot->onPresetRecall = [this, cid](int i) {
                auto cell = store_.cell(cid);
                if (i < 0 || i >= (int)cell->presets.size()) return;
                applyPreset(*cell->presets[(size_t)i]);
                // Recall changed widget values: commit a history node.
                store_.setWidgetSnap(doc::captureWidgets(
                    appCtx_.uiState, claimedPanels(),
                    store_.snapshot()->widgets.get()));
                store_.commit("recall preset");
            };
            slot->onPresetOverwrite = [this, cid](int i) {
                auto presets = store_.cell(cid)->presets;
                if (i < 0 || i >= (int)presets.size()) return;
                doc::Preset p = capturePreset(presetScope(cid));
                p.name = presets[(size_t)i]->name;
                presets[(size_t)i] =
                    std::make_shared<doc::Preset const>(std::move(p));
                commitPresets(cid, std::move(presets), "overwrite preset");
            };
            slot->onPresetRename = [this, cid](int i, juce::String n) {
                auto presets = store_.cell(cid)->presets;
                if (i < 0 || i >= (int)presets.size()) return;
                auto np = std::make_shared<doc::Preset>(*presets[(size_t)i]);
                np->name = n.toStdString();
                presets[(size_t)i] = std::move(np);
                commitPresets(cid, std::move(presets), "rename preset");
            };
            slot->onPresetDelete = [this, cid](int i) {
                auto presets = store_.cell(cid)->presets;
                if (i < 0 || i >= (int)presets.size()) return;
                presets.erase(presets.begin() + i);
                commitPresets(cid, std::move(presets), "delete preset");
            };
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
    // rebuildCells() re-seeds every editor from the store, so text typed
    // since the last eval/save has to land in the store first or it is lost.
    syncAllCellText();
    int index = selectedCell_ ? store_.indexOf(selectedCell_) + 1
                              : store_.cellCount();
    CellId id = store_.insertCell(index, kind);
    rebuildCells();
    selectCell(id);
}

void NotebookView::deleteSelectedCell() {
    if (!selectedCell_) return;
    syncAllCellText();   // rebuildCells() re-seeds every editor
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

String NotebookView::testCellEditorText(int index) {
    auto snap = store_.snapshot();
    if (index < 0 || index >= (int)snap->cells.size()) return {};
    auto* cc = cellFor(snap->cells[(size_t)index]->id);
    return cc && cc->hasEditor() ? cc->editorText() : String();
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

std::vector<NotebookView::HistoryRow> NotebookView::historyRows() {
    std::vector<HistoryRow> rows;
    doc::HistNode* cursor = store_.historyCursor();
    // Depth-first; siblings indent one level under a branch point.
    std::function<void(doc::HistNode*, int)> walk =
        [&](doc::HistNode* n, int depth) {
            juce::String label(n->label);
            if (n->children.size() > 1)
                label += " (" + juce::String((int)n->children.size())
                       + " branches)";
            rows.push_back({ n, depth, label, n == cursor });
            int childDepth = depth + (n->children.size() > 1 ? 1 : 0);
            for (auto& child : n->children) walk(child.get(), childDepth);
        };
    if (store_.historyRoot()) walk(store_.historyRoot(), 0);
    return rows;
}

void NotebookView::jumpToHistory(doc::HistNode* node) {
    if (!node) return;
    auto before = claimedPanels();
    if (auto snap = store_.jumpTo(node)) {
        doc::restoreWidgets(appCtx_, snap->widgets ? *snap->widgets
                                                   : doc::WidgetSnapList{},
                            before);
        rebuildCells();
    }
}

void NotebookView::toggleHistoryWindow() {
    // Show/raise, don't toggle: if the window already exists it may just be
    // hidden behind the main window, so bring it to the front. Closing it is
    // the window's own close button (which clears historyWindow_).
    if (historyWindow_) {
        historyWindow_->setVisible(true);
        historyWindow_->toFront(true);
        return;
    }
    historyWindow_ = std::make_unique<HistoryWindow>(
        *this, [this] { historyWindow_.reset(); });
}

bool NotebookView::testPresetsRoundTrip(std::string const& panel) {
    if (!appCtx_.uiState) return false;
    newDocument();
    // A Presets cell governs the Panel cells *after* it, up to the next
    // Presets cell -- so the panel must follow the presets cell.
    CellId presetsCell = store_.insertCell(store_.cellCount(),
                                           CellKind::Presets);
    CellId panelCell = store_.insertCell(store_.cellCount(),
                                         CellKind::Panel, panel);
    (void)panelCell;
    rebuildCells();

    doc::Preset p = capturePreset(presetScope(presetsCell));
    if (p.entries.empty()) return false;

    // Read + perturb slider "a" so recall has something to restore.
    double before = 0.0, perturbed = 0.0;
    {
        std::lock_guard<std::mutex> lock(appCtx_.uiState->mtx);
        auto* w = appCtx_.uiState->findByName(panel, "a");
        if (!w || w->values.empty()) return false;
        before = w->values[0];
        w->values[0] = before + 0.123;
        perturbed = w->values[0];
    }
    std::vector<std::shared_ptr<doc::Preset const>> bank;
    bank.push_back(std::make_shared<doc::Preset const>(p));
    store_.setCellPresets(presetsCell, bank);
    applyPreset(*bank[0]);

    double after = -999.0;
    {
        std::lock_guard<std::mutex> lock(appCtx_.uiState->mtx);
        if (auto* w = appCtx_.uiState->findByName(panel, "a");
            w && !w->values.empty())
            after = w->values[0];
    }
    return before != perturbed && after == before;
}

// ---------------------------------------------------------------------------
// Presets (mirror of ImGui NotebookPanel)
// ---------------------------------------------------------------------------

// Input widgets whose values a preset stores. Displays, momentary buttons,
// and piano rolls are left alone.
static bool presetStorableKind(bridge::UIWidgetKind k) {
    switch (k) {
        case bridge::UIWidgetKind::Slider:
        case bridge::UIWidgetKind::Range:
        case bridge::UIWidgetKind::Number:
        case bridge::UIWidgetKind::Toggle:
        case bridge::UIWidgetKind::XY:
        case bridge::UIWidgetKind::MultiSlider:
        case bridge::UIWidgetKind::Matrix:
            return true;
        default:
            return false;
    }
}

std::vector<std::string> NotebookView::presetScope(CellId id) const {
    std::vector<std::string> out;
    bool after = false;
    for (auto const& c : store_.snapshot()->cells) {
        if (c->id == id) { after = true; continue; }
        if (!after) continue;
        if (c->kind == CellKind::Presets) break;
        if (c->kind == CellKind::Panel) out.push_back(c->name);
    }
    return out;
}

doc::Preset NotebookView::capturePreset(
    std::vector<std::string> const& scope) const {
    doc::Preset p;
    if (!appCtx_.uiState) return p;
    std::lock_guard<std::mutex> lock(appCtx_.uiState->mtx);
    for (auto const& w : appCtx_.uiState->widgets) {
        if (!presetStorableKind(w->kind)) continue;
        bool inScope = false;
        for (auto const& root : scope) {
            if (bridge::panelUnderRoot(w->panel, root)) { inScope = true; break; }
        }
        if (!inScope) continue;
        p.entries.push_back({ w->panel, w->name, w->values });
    }
    return p;
}

void NotebookView::applyPreset(doc::Preset const& p) {
    if (!appCtx_.uiState) return;
    std::lock_guard<std::mutex> lock(appCtx_.uiState->mtx);
    for (auto const& e : p.entries) {
        bridge::UIWidget* w = appCtx_.uiState->findByName(e.panel, e.widget);
        if (!w || !presetStorableKind(w->kind)) continue;
        for (size_t i = 0; i < w->values.size() && i < e.values.size(); ++i)
            w->values[i] = e.values[i];
        w->dirtyEngine = true;
        w->dirtyCallback = true;
    }
    // Push the recalled values to the engine and fire onChange callbacks.
    dispatcher_.ensureRunning();
}

void NotebookView::commitPresets(
    CellId id, std::vector<std::shared_ptr<doc::Preset const>> presets,
    char const* label) {
    store_.setCellPresets(id, std::move(presets));
    store_.setWidgetSnap(doc::captureWidgets(
        appCtx_.uiState, claimedPanels(), store_.snapshot()->widgets.get()));
    store_.commit(label);
    if (auto* cc = cellFor(id)) cc->syncFromModel(*store_.cell(id));
    relayoutContent();
}

}
