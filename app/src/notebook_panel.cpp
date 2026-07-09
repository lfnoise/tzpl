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

#include "notebook_panel.hpp"

#include "content_hash.hpp"
#include "editor_panel.hpp"
#include "controls_panel.hpp"
#include "widget_draw.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_ui_state.hpp"
#include "repl_session.hpp"
#include "diagnostic.hpp"

#include "imgui.h"
#include "imgui_internal.h"  // ImGuiItemFlags_AllowOverlap: arrange overlay

#include <algorithm>
#include <fstream>
#include <functional>
#include <sstream>

using doc::Cell;
using doc::CellId;
using doc::CellKind;

NotebookPanel::NotebookPanel()
    : langDef_(EditorPanel::createTzopilotlDef())
{}

// ---------------------------------------------------------------------------
// Document lifecycle
// ---------------------------------------------------------------------------

void NotebookPanel::newDocument() {
    store_.reset(std::make_shared<doc::DocSnapshot const>(), "");
    runtime_.clear();
    runQueue_.clear();
    focusedCell_ = 0;
    open_ = true;
    // Start with one code cell so there's something to type into.
    store_.insertCell(0, CellKind::Code);
    store_.clearModified();
    store_.rerootHistory("new");
}

bool NotebookPanel::open(std::string const& path, bridge::AppContext& ctx,
                         std::string& err) {
    doc::LoadedHistory hist;
    auto snap = doc::loadDocument(ctx, path, err, &store_.interns(), &hist);
    if (!snap) return false;
    store_.reset(std::move(snap), path);
    runtime_.clear();
    runQueue_.clear();
    focusedCell_ = 0;
    open_ = true;
    queueRunOnLoad();
    if (hist.root) {
        // Capture the just-restored live widgets into the working snapshot
        // (interning collapses them onto the history's snaps when equal),
        // then install the restored tree. adoptHistory unifies the working
        // snapshot with the cursor's when content-equal, so undo/redo pick
        // up exactly where the document was saved.
        store_.setWidgetSnap(doc::captureWidgets(
            ctx.uiState, claimedPanels(), store_.snapshot()->widgets.get()));
        store_.adoptHistory(std::move(hist.root), hist.cursor);
    } else {
        rerootHistory("open", ctx);
    }
    return true;
}

bool NotebookPanel::save(bridge::AppContext& ctx, std::string& err) {
    return saveAs(store_.filePath(), ctx, err);
}

bool NotebookPanel::saveAs(std::string const& path, bridge::AppContext& ctx,
                           std::string& err) {
    if (path.empty()) {
        err = "no file path";
        return false;
    }
    syncAllCellText();
    if (!doc::saveDocument(store_, ctx, path, err)) return false;
    store_.setFilePath(path);
    store_.clearModified();
    return true;
}

void NotebookPanel::close() {
    open_ = false;
    runtime_.clear();
    runQueue_.clear();
    focusedCell_ = 0;
    store_.reset(std::make_shared<doc::DocSnapshot const>(), "");
    store_.clearModified();
}

std::vector<std::string> NotebookPanel::claimedPanels() const {
    std::vector<std::string> names;
    if (!open_) return names;
    for (auto const& c : store_.snapshot()->cells) {
        if (c->kind == CellKind::Panel) names.push_back(c->name);
    }
    return names;
}

void NotebookPanel::queueRunOnLoad() {
    for (auto const& c : store_.snapshot()->cells) {
        if (c->kind == CellKind::Code && c->runOnLoad)
            runQueue_.push_back(c->id);
    }
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

void NotebookPanel::commitHistory(std::string const& label,
                                  bridge::AppContext& ctx) {
    store_.setWidgetSnap(doc::captureWidgets(
        ctx.uiState, claimedPanels(), store_.snapshot()->widgets.get()));
    store_.commit(label);
}

void NotebookPanel::rerootHistory(std::string const& label,
                                  bridge::AppContext& ctx) {
    store_.setWidgetSnap(doc::captureWidgets(
        ctx.uiState, claimedPanels(), store_.snapshot()->widgets.get()));
    store_.rerootHistory(label);
}

void NotebookPanel::applySnapshot(doc::SnapshotPtr snap,
                                  bridge::AppContext& ctx,
                                  std::vector<std::string> const& panelsBefore) {
    if (!snap) return;

    // Resync cell editors whose text differs; drop runtime of removed cells.
    for (auto const& c : snap->cells) {
        auto it = runtime_.find(c->id);
        if (it == runtime_.end() || !it->second.editor) continue;
        if (it->second.editor->GetText() != c->text) {
            it->second.editor->SetText(c->text);
            it->second.lastEditTime = 0.0;
        }
    }
    for (auto it = runtime_.begin(); it != runtime_.end();) {
        bool live = false;
        for (auto const& c : snap->cells) {
            if (c->id == it->first) { live = true; break; }
        }
        it = live ? std::next(it) : runtime_.erase(it);
    }
    if (focusedCell_ && !store_.cell(focusedCell_)) focusedCell_ = 0;

    // Restore claimed-panel widgets; changed values are marked dirty and
    // re-sent by the normal per-frame dispatch (undo is audible).
    // Reconcile the union of panels claimed before and after the jump:
    // a panel whose cell doesn't exist on this side has no widgets in
    // the target snapshot either, so its live widgets are removed.
    if (snap->widgets) {
        auto panels = claimedPanels();
        for (auto const& p : panelsBefore) {
            bool have = false;
            for (auto const& q : panels)
                if (q == p) { have = true; break; }
            if (!have) panels.push_back(p);
        }
        doc::restoreWidgets(ctx, *snap->widgets, panels);
    }
}

void NotebookPanel::undoDocument(bridge::AppContext& ctx) {
    // A pending typing coalesce is its own edit: commit it first so undo
    // steps back to the pre-typing state, not past it.
    for (auto const& c : store_.snapshot()->cells) {
        auto it = runtime_.find(c->id);
        if (it != runtime_.end() && it->second.lastEditTime > 0.0) {
            syncCellText(c->id);
            it->second.lastEditTime = 0.0;
        }
    }
    commitHistory("edit", ctx);
    auto before = claimedPanels();
    applySnapshot(store_.undo(), ctx, before);
}

void NotebookPanel::redoDocument(bridge::AppContext& ctx) {
    auto before = claimedPanels();
    applySnapshot(store_.redo(), ctx, before);
}

void NotebookPanel::update(bridge::AppContext& ctx) {
    if (!open_) return;

    // One history commit per finished widget gesture (claimed panels only;
    // floating-panel tweaks are performance state outside the document).
    if (ctx.uiState) {
        std::string gestured;
        auto claimed = claimedPanels();
        {
            std::lock_guard<std::mutex> lock(ctx.uiState->mtx);
            for (auto& w : ctx.uiState->widgets) {
                if (!w->gestureEnded) continue;
                w->gestureEnded = false;
                bool claimedPanel = false;
                for (auto const& root : claimed) {
                    if (bridge::panelUnderRoot(w->panel, root)) {
                        claimedPanel = true;
                        break;
                    }
                }
                if (claimedPanel && w->kind != bridge::UIWidgetKind::Button) {
                    if (!gestured.empty()) gestured += ", ";
                    gestured += w->name;
                }
            }
        }
        if (!gestured.empty()) commitHistory("adjust " + gestured, ctx);
    }

    // Typing coalesce: ~1s after the last keystroke, sync + commit once.
    double now = ImGui::GetTime();
    for (auto const& c : store_.snapshot()->cells) {
        auto it = runtime_.find(c->id);
        if (it == runtime_.end() || it->second.lastEditTime == 0.0) continue;
        if (now - it->second.lastEditTime > 1.0) {
            it->second.lastEditTime = 0.0;
            syncCellText(c->id);
            commitHistory("edit", ctx);
        }
    }

    // Deferred single commits for structural edits and finished evals.
    if (!structureCommitLabel_.empty()) {
        commitHistory(structureCommitLabel_, ctx);
        structureCommitLabel_.clear();
    }
    if (!evalCommitLabel_.empty()) {
        commitHistory(evalCommitLabel_, ctx);
        evalCommitLabel_.clear();
    }
}

// ---------------------------------------------------------------------------
// Cell runtime
// ---------------------------------------------------------------------------

// Prose cells read as text, not code: no line numbers, warm ivory type on
// a slightly lifted background. (A proportional font / styled prose editor
// is future work -- ImGui fonts are per-atlas, so mixing families needs a
// second loaded font.)
static TextEditor::Palette const& prosePalette() {
    static TextEditor::Palette p = [] {
        TextEditor::Palette pal = TextEditor::GetDarkPalette();
        pal[(int)TextEditor::PaletteIndex::Default] = 0xffb0ccda;    // warm ivory
        pal[(int)TextEditor::PaletteIndex::Identifier] = 0xffb0ccda;
        pal[(int)TextEditor::PaletteIndex::Number] = 0xffb0ccda;
        pal[(int)TextEditor::PaletteIndex::Punctuation] = 0xffb0ccda;
        pal[(int)TextEditor::PaletteIndex::Background] = 0xff181614; // lifted
        pal[(int)TextEditor::PaletteIndex::CurrentLineFill] = 0x00000000;
        pal[(int)TextEditor::PaletteIndex::CurrentLineFillInactive] = 0x00000000;
        pal[(int)TextEditor::PaletteIndex::CurrentLineEdge] = 0x00000000;
        return pal;
    }();
    return p;
}

NotebookPanel::CellRuntime& NotebookPanel::runtime(CellId id, Cell const& cell) {
    auto it = runtime_.find(id);
    if (it != runtime_.end()) return it->second;

    CellRuntime rt;
    if (cell.kind == CellKind::Code || cell.kind == CellKind::Prose) {
        rt.editor = std::make_unique<TextEditor>();
        if (cell.kind == CellKind::Code) {
            rt.editor->SetLanguageDefinition(langDef_);
        } else {
            rt.editor->SetShowLineNumbers(false);
            rt.editor->SetPalette(prosePalette());
        }
        rt.editor->SetShowWhitespaces(false);
        rt.editor->SetText(cell.text);
    }
    return runtime_.emplace(id, std::move(rt)).first->second;
}

static std::string editorText(TextEditor& ed) {
    // TextEditor::GetText appends a trailing newline; keep it (harmless for
    // eval, stable for hashing/compare as long as we're consistent).
    return ed.GetText();
}

void NotebookPanel::syncCellText(CellId id) {
    auto it = runtime_.find(id);
    if (it == runtime_.end() || !it->second.editor) return;
    store_.setCellText(id, editorText(*it->second.editor));
}

void NotebookPanel::syncAllCellText() {
    for (auto const& c : store_.snapshot()->cells) {
        syncCellText(c->id);
    }
}

bool NotebookPanel::anyEditorDirty() const {
    for (auto const& c : store_.snapshot()->cells) {
        auto it = runtime_.find(c->id);
        if (it == runtime_.end() || !it->second.editor) continue;
        if (it->second.editor->GetText() != c->text) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Eval
// ---------------------------------------------------------------------------

void NotebookPanel::launchCell(CellId id, GuiState& gui,
                               bridge::AppContext& ctx,
                               ts::REPLSession* session) {
    if (!session || gui.asyncEval.busy()) return;
    auto cell = store_.cell(id);
    if (!cell || cell->kind != CellKind::Code) return;

    syncCellText(id);
    cell = store_.cell(id);
    auto& rt = runtime(id, *cell);
    rt.output.clear();
    rt.lastErrored = false;
    if (rt.editor) rt.editor->SetErrorMarkers({});
    gui.asyncEval.launch(cell->text, ctx, *session, -1, -1, id);
}

void NotebookPanel::runFocused(GuiState& gui, bridge::AppContext& ctx,
                               ts::REPLSession* session) {
    if (focusedCell_) launchCell(focusedCell_, gui, ctx, session);
}

TextEditor* NotebookPanel::focusedEditor() {
    if (!open_ || !focusedCell_) return nullptr;
    auto it = runtime_.find(focusedCell_);
    return it != runtime_.end() ? it->second.editor.get() : nullptr;
}

void NotebookPanel::moveHome(bool select) {
    if (auto* ed = focusedEditor()) ed->MoveHome(select);
}
void NotebookPanel::moveEnd(bool select) {
    if (auto* ed = focusedEditor()) ed->MoveEnd(select);
}
void NotebookPanel::moveTop(bool select) {
    if (auto* ed = focusedEditor()) ed->MoveTop(select);
}
void NotebookPanel::moveBottom(bool select) {
    if (auto* ed = focusedEditor()) ed->MoveBottom(select);
}

void NotebookPanel::cutText() {
    if (auto* ed = focusedEditor()) ed->Cut();
}
void NotebookPanel::copyText() {
    if (auto* ed = focusedEditor()) ed->Copy();
}
void NotebookPanel::pasteText() {
    if (auto* ed = focusedEditor()) ed->Paste();
}
void NotebookPanel::selectAllText() {
    if (auto* ed = focusedEditor()) ed->SelectAll();
}
bool NotebookPanel::undoText() {
    auto* ed = focusedEditor();
    if (!ed) return false;
    ed->Undo();
    return true;
}
bool NotebookPanel::redoText() {
    auto* ed = focusedEditor();
    if (!ed) return false;
    ed->Redo();
    return true;
}

bool NotebookPanel::openFileIntoFocusedCell(std::string const& path) {
    if (!open_ || !focusedCell_) return false;
    auto cell = store_.cell(focusedCell_);
    if (!cell || cell->kind != CellKind::Code) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    // Insert at the cell's cursor (replacing any selection), like a
    // paste -- Select All first to replace the whole cell.
    auto& rt = runtime(focusedCell_, *cell);
    if (!rt.editor) return false;
    if (rt.editor->HasSelection()) rt.editor->Delete();
    rt.editor->InsertText(ss.str());
    syncCellText(focusedCell_);
    rt.lastEditTime = 0.0;
    structureCommitLabel_ = "open file into cell";  // commits in update()
    return true;
}

void NotebookPanel::runAll() {
    runQueue_.clear();
    for (auto const& c : store_.snapshot()->cells) {
        if (c->kind == CellKind::Code) runQueue_.push_back(c->id);
    }
}

void NotebookPanel::pumpRunQueue(GuiState& gui, bridge::AppContext& ctx,
                                 ts::REPLSession* session) {
    if (runQueue_.empty() || gui.asyncEval.busy()) return;
    CellId id = runQueue_.front();
    runQueue_.pop_front();
    launchCell(id, gui, ctx, session);
}

void NotebookPanel::onEvalDone(std::uint64_t cellId,
                               ts::REPLSession::EvalResult const& result,
                               std::string const& code) {
    auto cell = store_.cell(cellId);
    if (!cell) return;
    auto& rt = runtime(cellId, *cell);
    rt.lastEditTime = 0.0;  // launchCell synced the text already
    rt.everRan = true;

    if (!result.errors.empty()) {
        rt.lastErrored = true;
        if (!runQueue_.empty()) {
            // Run All stops on first error; say what was skipped.
            rt.output.push_back({"Run All stopped here -- "
                                     + std::to_string(runQueue_.size())
                                     + " queued cell(s) not run",
                                 LineKind::Error});
            runQueue_.clear();
        }
        auto formatted = ts::formatErrorsPlain(result.errors, code, "<cell>");
        for (auto& line : formatted) {
            rt.output.push_back({line, LineKind::Error});
        }
        if (rt.editor) {
            TextEditor::ErrorMarkers markers;
            for (auto const& e : result.errors) {
                markers[(int)e.loc.start.line] = e.message;
            }
            rt.editor->SetErrorMarkers(markers);
        }
    } else {
        rt.lastErrored = false;
        rt.ranTextHash = std::hash<std::string>{}(code);
        if (result.hasValue) {
            rt.output.push_back({"\xe2\x86\x92 " + result.prettyValue
                                 + " : " + result.typeName, LineKind::Result});
        }
    }
    // One history node per eval: the synced text plus whatever widgets the
    // code created or changed. Committed in update(), which has the
    // AppContext for the widget capture.
    evalCommitLabel_ = "run cell";
}

void NotebookPanel::addCellOutput(std::uint64_t cellId,
                                  std::string const& line, LineKind kind) {
    auto cell = store_.cell(cellId);
    if (!cell) return;
    auto& rt = runtime(cellId, *cell);
    rt.output.push_back({line, kind});
    constexpr size_t kMaxLines = 500;
    if (rt.output.size() > kMaxLines) {
        rt.output.erase(rt.output.begin(),
                        rt.output.end() - (long)kMaxLines);
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Presets cells (a matrix of control snapshots)
// ---------------------------------------------------------------------------

// Kinds whose values a preset stores/recalls: the input widgets.
// Displays (meter/scope/plot/waveform/label), momentary buttons, and
// piano rolls (note lists, not values) are left alone.
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

std::vector<std::string> NotebookPanel::presetScope(doc::CellId id) const {
    // The panel cells after this presets cell, up to the next one.
    std::vector<std::string> out;
    bool after = false;
    for (auto const& c : store_.snapshot()->cells) {
        if (c->id == id) {
            after = true;
            continue;
        }
        if (!after) continue;
        if (c->kind == CellKind::Presets) break;
        if (c->kind == CellKind::Panel) out.push_back(c->name);
    }
    return out;
}

doc::Preset NotebookPanel::capturePreset(
    std::vector<std::string> const& scope, bridge::AppContext& ctx) {
    doc::Preset p;
    if (!ctx.uiState) return p;
    std::lock_guard<std::mutex> lock(ctx.uiState->mtx);
    for (auto const& w : ctx.uiState->widgets) {
        if (!presetStorableKind(w->kind)) continue;
        bool inScope = false;
        for (auto const& root : scope) {
            if (bridge::panelUnderRoot(w->panel, root)) {
                inScope = true;
                break;
            }
        }
        if (!inScope) continue;
        p.entries.push_back({w->panel, w->name, w->values});
    }
    return p;
}

void NotebookPanel::applyPreset(doc::Preset const& p,
                                bridge::AppContext& ctx) {
    if (!ctx.uiState) return;
    std::lock_guard<std::mutex> lock(ctx.uiState->mtx);
    for (auto const& e : p.entries) {
        bridge::UIWidget* w = ctx.uiState->findByName(e.panel, e.widget);
        if (!w || !presetStorableKind(w->kind)) continue;
        for (size_t i = 0; i < w->values.size() && i < e.values.size(); ++i)
            w->values[i] = e.values[i];
        w->dirtyEngine = true;
        w->dirtyCallback = true;
    }
}

void NotebookPanel::drawPresetsCell(
    std::shared_ptr<doc::Cell const> const& cell, CellRuntime& rt,
    float width, bridge::AppContext& ctx) {
    auto presets = cell->presets;  // COW: edit a copy, install via store_
    doc::CellId id = cell->id;
    int selected = rt.selectedPreset;
    if (selected >= (int)presets.size()) selected = -1;

    // Slots are shared immutable Presets: editing one replaces that
    // pointer; the rest stay shared across history snapshots.
    auto renameSlot = [&](int i, char const* name) {
        auto np = std::make_shared<doc::Preset>(*presets[(size_t)i]);
        np->name = name;
        presets[(size_t)i] = std::move(np);
    };

    // Top row: store new / overwrite selected / name / delete.
    if (ImGui::SmallButton("+ store")) {
        presets.push_back(std::make_shared<doc::Preset const>(
            capturePreset(presetScope(id), ctx)));
        selected = (int)presets.size() - 1;
        store_.setCellPresets(id, presets);
        structureCommitLabel_ = "store preset";
    }
    if (selected >= 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("overwrite")) {
            doc::Preset p = capturePreset(presetScope(id), ctx);
            p.name = presets[(size_t)selected]->name;
            presets[(size_t)selected] =
                std::make_shared<doc::Preset const>(std::move(p));
            store_.setCellPresets(id, presets);
            structureCommitLabel_ = "overwrite preset";
        }
        ImGui::SameLine();
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s",
                      presets[(size_t)selected]->name.c_str());
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputTextWithHint("##presetname", "(name)", nameBuf,
                                     sizeof(nameBuf))) {
            renameSlot(selected, nameBuf);
            store_.setCellPresets(id, presets);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            structureCommitLabel_ = "rename preset";
        ImGui::SameLine();
        if (ImGui::SmallButton("delete")) {
            presets.erase(presets.begin() + selected);
            selected = -1;
            store_.setCellPresets(id, presets);
            structureCommitLabel_ = "delete preset";
        }
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled(presets.empty()
                                ? "(stores the controls of the panels "
                                  "below, until the next presets cell)"
                                : "(click recalls; right- or cmd-click "
                                  "names / overwrites / deletes)");
    }

    // The slot matrix. Click = recall (+select); Cmd-click = overwrite;
    // right-click = context menu (rename without recalling).
    float slotW = 84.0f;
    int perRow = std::max(1, (int)((width - 16.0f)
                                   / (slotW + ImGui::GetStyle().ItemSpacing.x)));
    int overwriteIdx = -1, deleteIdx = -1;
    bool renamed = false;
    for (int i = 0; i < (int)presets.size(); ++i) {
        if (i % perRow != 0) ImGui::SameLine();
        ImGui::PushID(i);
        char label[80];
        if (presets[(size_t)i]->name.empty())
            std::snprintf(label, sizeof(label), "%d", i + 1);
        else
            std::snprintf(label, sizeof(label), "%s",
                          presets[(size_t)i]->name.c_str());
        bool isSel = i == selected;
        if (isSel)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(
                                      ImGuiCol_ButtonActive));
        if (ImGui::Button(label, ImVec2(slotW, 0.0f))) {
            selected = i;
            if (ImGui::GetIO().KeySuper || ImGui::GetIO().KeyCtrl) {
                // Cmd-click = the context menu, same as right-click.
                ImGui::OpenPopup("##slotmenu");
            } else {
                applyPreset(*presets[(size_t)i], ctx);
                structureCommitLabel_ = "recall preset";
            }
        }
        if (isSel) ImGui::PopStyleColor();
        // Right-click / cmd-click: select (without recalling) and edit
        // in place.
        if (ImGui::BeginPopupContextItem("##slotmenu")) {
            selected = i;
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s",
                          presets[(size_t)i]->name.c_str());
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::InputTextWithHint("##rename", "(name)", nameBuf,
                                         sizeof(nameBuf))) {
                renameSlot(i, nameBuf);
                renamed = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
                structureCommitLabel_ = "rename preset";
            // Enter in the name field dismisses the menu.
            if (ImGui::IsItemDeactivated()
                && (ImGui::IsKeyPressed(ImGuiKey_Enter)
                    || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))
                ImGui::CloseCurrentPopup();
            if (ImGui::MenuItem("Overwrite with current values"))
                overwriteIdx = i;
            if (ImGui::MenuItem("Delete")) deleteIdx = i;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (overwriteIdx >= 0) {
        doc::Preset p = capturePreset(presetScope(id), ctx);
        p.name = presets[(size_t)overwriteIdx]->name;
        presets[(size_t)overwriteIdx] =
            std::make_shared<doc::Preset const>(std::move(p));
        store_.setCellPresets(id, presets);
        structureCommitLabel_ = "overwrite preset";
    } else if (deleteIdx >= 0) {
        presets.erase(presets.begin() + deleteIdx);
        if (selected == deleteIdx) selected = -1;
        else if (selected > deleteIdx) --selected;
        store_.setCellPresets(id, presets);
        structureCommitLabel_ = "delete preset";
    } else if (renamed) {
        store_.setCellPresets(id, presets);
    }
    rt.selectedPreset = selected;
}

// The frame (position, or size for the resize grip) at the instant an
// arrange drag began. One drag is active at a time, so shared state is
// fine -- and move/resize can never both be active.
static ImVec2 gArrangeAnchor(0.0f, 0.0f);

static ImVec4 kindColor(bool everRan, bool errored, bool stale) {
    if (errored) return ImVec4(0.90f, 0.30f, 0.30f, 1.0f);   // red
    if (!everRan) return ImVec4(0.45f, 0.45f, 0.45f, 1.0f);  // hollow gray
    if (stale) return ImVec4(0.95f, 0.75f, 0.25f, 1.0f);     // amber
    return ImVec4(0.35f, 0.75f, 0.40f, 1.0f);                // green
}

void NotebookPanel::drawCellOutput(CellRuntime& rt, float width) {
    if (rt.output.empty()) return;
    // Fit the content (up to a cap), with room for the frame padding
    // and the horizontal scrollbar, so short output isn't clipped.
    float lineH = ImGui::GetTextLineHeightWithSpacing();
    float pad = ImGui::GetStyle().WindowPadding.y * 2.0f
              + ImGui::GetStyle().ScrollbarSize;
    float autoH = std::min((float)rt.output.size(), 12.0f) * lineH + pad;
    float h = rt.outputHeight > 0.0f ? rt.outputHeight : autoH;
    ImGui::BeginChild("##cellout", ImVec2(width, h), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (auto const& line : rt.output) {
        switch (line.kind) {
            case LineKind::Error:
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s",
                                   line.text.c_str());
                break;
            case LineKind::Result:
                ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "%s",
                                   line.text.c_str());
                break;
            default:
                ImGui::TextUnformatted(line.text.c_str());
                break;
        }
    }
    ImGui::EndChild();

    // Drag bar: resize the output pane (session-only, like scroll state).
    ImGui::InvisibleButton("##outsplit", ImVec2(width, 5.0f));
    if (ImGui::IsItemActive()) {
        float base = rt.outputHeight > 0.0f ? rt.outputHeight : h;
        rt.outputHeight = std::max(lineH + pad,
                                   base + ImGui::GetIO().MouseDelta.y);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
}

void NotebookPanel::drawCell(std::shared_ptr<Cell const> const& cell,
                             float width, GuiState& gui,
                             bridge::AppContext& ctx,
                             ts::REPLSession* session,
                             ControlsPanel& controls) {
    CellId id = cell->id;
    CellRuntime& rt = runtime(id, *cell);
    ImGui::PushID((int)id);
    ImVec2 cellTop = ImGui::GetCursorScreenPos();

    // ---- header row -------------------------------------------------------
    char const* kindName = cell->kind == CellKind::Code ? "code"
                         : cell->kind == CellKind::Prose ? "prose"
                         : cell->kind == CellKind::Presets ? "presets"
                                                           : "panel";

    bool stale = false;
    if (cell->kind == CellKind::Code && rt.editor) {
        stale = rt.everRan
             && std::hash<std::string>{}(editorText(*rt.editor)) != rt.ranTextHash;
    }

    // Collapse triangle: body hidden, header strip stays (all cell
    // kinds). Document state, saved with the file; not its own history
    // step -- it rides along with the next commit.
    {
        ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(pad.x * 0.5f, 0.0f));
        if (ImGui::ArrowButton("##collapse", cell->collapsed
                                                 ? ImGuiDir_Right
                                                 : ImGuiDir_Down)) {
            store_.setCellCollapsed(id, !cell->collapsed);
        }
        ImGui::PopStyleVar();
        ImGui::SameLine();
    }

    // Staleness / status dot
    ImVec2 dotPos = ImGui::GetCursorScreenPos();
    float lineH = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(dotPos.x + lineH * 0.5f, dotPos.y + lineH * 0.6f),
        3.5f, ImGui::GetColorU32(kindColor(rt.everRan, rt.lastErrored, stale)));
    ImGui::Dummy(ImVec2(lineH, lineH));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", kindName);

    if (cell->kind == CellKind::Panel) {
        // Editable panel name: this is the name code targets with
        // ui.panel(...) / controls(node, "name").
        ImGui::SameLine();
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", cell->name.c_str());
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputText("##panelname", nameBuf, sizeof(nameBuf))) {
            store_.setCellName(id, nameBuf);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            structureCommitLabel_ = "rename panel";
        }
        ImGui::SameLine();
        ImGui::Checkbox("arrange", &rt.arrange);
        // Unpin every widget in this panel back to code-order flow
        // (frames from arrange drags, setFrame, or files saved by
        // older builds -- which pinned auto-flowed positions).
        ImGui::SameLine();
        if (ImGui::SmallButton("reflow") && ctx.uiState) {
            std::lock_guard<std::mutex> lock(ctx.uiState->mtx);
            for (auto& wp : ctx.uiState->widgets) {
                if (bridge::panelUnderRoot(wp->panel, cell->name)) {
                    wp->fx = -1.0f;
                    wp->fy = -1.0f;
                }
            }
            structureCommitLabel_ = "reflow panel";
        }
    }

    if (cell->kind == CellKind::Code || cell->kind == CellKind::Presets) {
        // Editable label so cells can be identified (saved with the
        // document; purely descriptive).
        ImGui::SameLine();
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", cell->name.c_str());
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputTextWithHint("##cellname", "(name)", nameBuf,
                                     sizeof(nameBuf))) {
            store_.setCellName(id, nameBuf);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            structureCommitLabel_ = "rename cell";
        }
    }

    if (cell->kind == CellKind::Code) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Run")) {
            focusedCell_ = id;
            if (!gui.asyncEval.busy()) launchCell(id, gui, ctx, session);
            else runQueue_.push_back(id);
        }
        ImGui::SameLine();
        bool rol = cell->runOnLoad;
        if (ImGui::Checkbox("run on load", &rol)) {
            store_.setCellRunOnLoad(id, rol);
            structureCommitLabel_ = "toggle run-on-load";
        }
    }

    // Right-aligned structure buttons
    float btnW = ImGui::CalcTextSize("\xe2\x86\x91  \xe2\x86\x93  x").x + 60.0f;
    ImGui::SameLine(std::max(width - btnW, 0.0f));
    if (ImGui::SmallButton("\xe2\x86\x91"))  // up arrow
        { pendingMove_ = id; pendingMoveDelta_ = -1; }
    ImGui::SameLine();
    if (ImGui::SmallButton("\xe2\x86\x93"))  // down arrow
        { pendingMove_ = id; pendingMoveDelta_ = 1; }
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) { pendingDelete_ = id; }

    // ---- body (hidden while collapsed) --------------------------------------
    if (cell->collapsed) {
        // Header strip only.
    } else if (cell->kind == CellKind::Presets) {
        drawPresetsCell(cell, rt, width, ctx);
    } else if (cell->kind == CellKind::Panel) {
        if (ctx.uiState) {
            // The canvas grows to fit its widgets (measured last frame);
            // the splitter's panelHeight sets a minimum beyond that.
            float canvasH = std::max({60.0f, cell->panelHeight,
                                      rt.panelContentH});
            rt.panelContentH = drawPanelCanvas(cell, width, ctx, controls,
                                               rt.arrange, 1.0f, canvasH);

            // Canvas height splitter.
            ImGui::InvisibleButton("##panelheight", ImVec2(width, 6.0f));
            if (ImGui::IsItemActive()) {
                store_.setCellPanelHeight(
                    id, std::max(60.0f,
                                 cell->panelHeight
                                     + ImGui::GetIO().MouseDelta.y));
            }
            if (ImGui::IsItemDeactivated()) {
                structureCommitLabel_ = "resize panel";
            }
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }
    } else if (rt.editor) {
        int lines = std::max(rt.editor->GetTotalLines(), 2);
        float lineH = ImGui::GetTextLineHeightWithSpacing();
        float autoH = std::min((float)lines + 1.5f, 25.0f) * lineH;
        float editorH = rt.editorHeight > 0.0f ? rt.editorHeight : autoH;
        rt.editor->Render("##celledit", ImVec2(width, editorH), true);
        if (rt.editor->IsTextChanged()) {
            rt.lastEditTime = ImGui::GetTime();
        }
        if (cell->kind == CellKind::Code) {
            // Split between the text and its output: drag to resize the
            // editor area (session-only, like the output pane's bar).
            ImGui::InvisibleButton("##editsplit", ImVec2(width, 5.0f));
            if (ImGui::IsItemActive()) {
                float base = rt.editorHeight > 0.0f ? rt.editorHeight
                                                    : editorH;
                rt.editorHeight = std::max(2.0f * lineH,
                                           base + ImGui::GetIO().MouseDelta.y);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            drawCellOutput(rt, width);
        }
    }

    // Selection: clicking anywhere in the cell selects it. New cells are
    // inserted after the selected cell, which shows an accent bar.
    ImVec2 cellBottom = ImGui::GetCursorScreenPos();
    cellBottom.x = cellTop.x + width;
    if (ImGui::IsMouseClicked(0)
        && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
        && ImGui::IsMouseHoveringRect(cellTop, cellBottom)) {
        focusedCell_ = id;
    }
    if (focusedCell_ == id) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(cellTop.x - 8.0f, cellTop.y),
            ImVec2(cellTop.x - 5.0f, cellBottom.y),
            ImGui::GetColorU32(ImGuiCol_SliderGrab));
    }

    ImGui::PopID();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

float NotebookPanel::drawPanelCanvas(
    std::shared_ptr<doc::Cell const> const& cell, float width,
    bridge::AppContext& ctx, ControlsPanel& controls, bool arrange,
    float scale, float canvasH) {
    // Sub-panels "<cell>/<tab>" show as tabs; the bare cell name is
    // the first page. Distinct names gathered in registry order.
    std::vector<std::string> pages;
    {
        std::lock_guard<std::mutex> lock(ctx.uiState->mtx);
        bool bare = false;
        for (auto& wp : ctx.uiState->widgets) {
            if (wp->panel == cell->name) {
                bare = true;
            } else if (bridge::panelUnderRoot(wp->panel, cell->name)
                       && std::find(pages.begin(), pages.end(), wp->panel)
                              == pages.end()) {
                pages.push_back(wp->panel);
            }
        }
        if (bare || pages.empty())
            pages.insert(pages.begin(), cell->name);
    }
    if (pages.size() <= 1) {
        return drawPanelPage(pages[0], width, ctx, controls, arrange,
                             scale, canvasH);
    }
    // Tab bar + the selected page below it. Selection is ImGui view
    // state (keyed by the panel name), never document state.
    float tabH = ImGui::GetFrameHeight() / scale;
    float content = 52.0f;
    if (ImGui::BeginTabBar("##panelpages")) {
        for (auto const& p : pages) {
            std::string label = p == cell->name
                              ? std::string("(main)")
                              : p.substr(cell->name.size() + 1);
            if (ImGui::BeginTabItem((label + "###" + p).c_str())) {
                content = drawPanelPage(p, width, ctx, controls, arrange,
                                        scale,
                                        std::max(60.0f, canvasH - tabH));
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    return content + tabH;
}

float NotebookPanel::drawPanelPage(
    std::string const& panel, float width, bridge::AppContext& ctx,
    ControlsPanel& controls, bool arrange, float scale, float canvasH) {
    float contentBottom = 52.0f;
    ImGui::BeginChild(("##pc" + panel).c_str(),
                      ImVec2(width, canvasH * scale), true,
                      ImGuiWindowFlags_NoScrollWithMouse);
    if (scale != 1.0f) {
        ImGui::SetWindowFontScale(scale);
        setUIDrawScale(scale);
    }
    {
        std::lock_guard<std::mutex> lock(ctx.uiState->mtx);
        bool any = false;
        float autoY = 8.0f;
        auto snap8 = [](float v) {
            return std::round(v / 8.0f) * 8.0f;
        };
        // The flow cursor advances in whole grid steps, so a flowing
        // widget's y is always a multiple of 8 -- an arranged widget,
        // whose frame snaps to the same grid, can then line up with it.
        // (Its x already can: the flow margin below is 8.)
        auto snap8up = [](float v) {
            return std::ceil(v / 8.0f) * 8.0f;
        };
        // Lay out in last-upsert order (seq), so re-running the
        // creating code with calls reordered reflows to match.
        std::vector<bridge::UIWidget*> ws;
        for (auto& wp : ctx.uiState->widgets)
            if (wp->panel == panel) ws.push_back(wp.get());
        std::sort(ws.begin(), ws.end(),
                  [](bridge::UIWidget* a, bridge::UIWidget* b) {
                      return a->seq < b->seq;
                  });
        for (auto* wpp : ws) {
            bridge::UIWidget& w = *wpp;
            any = true;

            // Unplaced widgets (fx < 0) FLOW: top to bottom in creation
            // order, recomputed every frame -- so editing the creating
            // code re-lays them out on the next run. A frame becomes
            // sticky document state only via an arrange drag or
            // setFrame(); those widgets stay where they were put.
            float px = w.fx, py = w.fy;
            if (w.fx < 0.0f) {
                px = 8.0f;
                py = autoY;
            }

            ImGui::SetCursorPos(ImVec2(px * scale, py * scale));
            if (arrange) {
                ImGui::BeginDisabled();
                // A DISABLED item still claims HoveredId -- imgui.cpp's
                // ItemHoverable() sets it before its disabled early-out --
                // and a claimed HoveredId blocks every later overlapping
                // item. Without AllowOverlap the arrange overlay submitted
                // below can only be grabbed where the widget happened to
                // submit no item of its own, i.e. its text label.
                ImGui::PushItemFlag(ImGuiItemFlags_AllowOverlap, true);
            }
            ImGui::BeginGroup();
            if (drawUIWidget(w)) controls.noteTapsVisible();
            ImGui::EndGroup();
            if (arrange) {
                ImGui::PopItemFlag();
                ImGui::EndDisabled();
            }

            // Advance the flow cursor past the widget's ACTUAL drawn
            // extent (an xy pad or piano roll is far taller than a
            // slider row), back in unscaled canvas coordinates.
            float itemH = (ImGui::GetItemRectMax().y
                           - ImGui::GetItemRectMin().y) / scale;
            autoY = std::max(autoY,
                             snap8up(py + std::max(itemH, 24.0f) + 8.0f));
            contentBottom = std::max(contentBottom,
                                     py + std::max(itemH, 24.0f));

            if (arrange) {
                // Drag overlay: move; corner grip: resize. Both snap to
                // the 8px grid live, and commit one history node on
                // release (via gestureEnded). Arrange only runs at scale 1.
                //
                // The snap quantizes (frame at mouse-down + total drag
                // delta) rather than accumulating per-frame deltas into
                // the already-snapped frame -- the latter would swallow
                // every mouse motion smaller than the grid.
                ImVec2 rmin = ImGui::GetItemRectMin();
                ImVec2 rmax = ImGui::GetItemRectMax();
                ImGui::PushID((int)w.id);
                // The grip is submitted BEFORE the move overlay it sits
                // inside: overlapping items are first-come-first-served
                // (the first to claim HoveredId wins), so the smaller,
                // more specific target has to go first.
                float const kGrip = 12.0f;   // hit area == the drawn square
                ImVec2 gripMin(rmax.x - kGrip, rmax.y - kGrip);
                ImGui::SetCursorScreenPos(gripMin);
                ImGui::InvisibleButton("##size", ImVec2(kGrip, kGrip));
                if (ImGui::IsItemActivated()) {
                    gArrangeAnchor =
                        ImVec2(w.fw > 0.0f ? w.fw : rmax.x - rmin.x,
                               w.fh > 0.0f ? w.fh : rmax.y - rmin.y);
                }
                if (ImGui::IsItemActive()) {
                    ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left,
                                                        0.0f);
                    w.fw = std::max(24.0f, snap8(gArrangeAnchor.x + d.x));
                    w.fh = std::max(14.0f, snap8(gArrangeAnchor.y + d.y));
                    w.gestureActive = true;
                } else if (ImGui::IsItemDeactivated()) {
                    w.gestureActive = false;
                    w.gestureEnded = true;
                }
                // Move overlay: the whole widget, grip corner excepted.
                ImGui::SetCursorScreenPos(rmin);
                ImGui::InvisibleButton("##move",
                    ImVec2(std::max(rmax.x - rmin.x, 16.0f),
                           std::max(rmax.y - rmin.y, 16.0f)));
                if (ImGui::IsItemActivated()) {
                    // Dragging a flowing widget pins it: its current
                    // flow position becomes the sticky frame.
                    if (w.fx < 0.0f) {
                        w.fx = px;
                        w.fy = py;
                    }
                    gArrangeAnchor = ImVec2(w.fx, w.fy);
                }
                if (ImGui::IsItemActive()) {
                    ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left,
                                                        0.0f);
                    w.fx = std::max(0.0f, snap8(gArrangeAnchor.x + d.x));
                    w.fy = std::max(0.0f, snap8(gArrangeAnchor.y + d.y));
                    w.gestureActive = true;
                } else if (ImGui::IsItemDeactivated()) {
                    w.gestureActive = false;
                    w.gestureEnded = true;
                }
                // Move border + a filled bottom-right resize grip, so the
                // corner that resizes is visible (matches the JUCE app).
                ImU32 arrangeCol = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
                auto* dl = ImGui::GetWindowDrawList();
                dl->AddRect(rmin, rmax, arrangeCol);
                dl->AddRectFilled(gripMin, rmax, arrangeCol);
                ImGui::PopID();
            }
        }
        if (!any) {
            ImGui::SetCursorPos(ImVec2(8.0f, 8.0f));
            ImGui::TextDisabled("(no widgets -- run code that calls "
                                "controls(node, \"%s\") or "
                                "ui.panel(\"%s\") and creates some)",
                                panel.c_str(), panel.c_str());
        }
    }
    if (scale != 1.0f) {
        setUIDrawScale(1.0f);
        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::EndChild();
    return contentBottom + 8.0f;
}

void NotebookPanel::drawPerform(float width, float height,
                                bridge::AppContext& ctx,
                                ControlsPanel& controls) {
    ImGui::BeginChild("##perform", ImVec2(width, height), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (ImGui::SmallButton("Exit Perform")) performMode_ = false;
    ImGui::SameLine();
    {
        std::string title = store_.filePath().empty()
                          ? std::string("(untitled notebook)")
                          : store_.filePath();
        ImGui::TextDisabled("%s -- esc exits; widget keys are live",
                            title.c_str());
    }
    ImGui::Separator();

    if (!ctx.uiState) {
        ImGui::EndChild();
        return;
    }
    // One tab per panel cell: separate control sets, one showing at a
    // time (the HyperCard card model). Tab selection is pure view
    // state -- ImGui remembers it; nothing touches the document or
    // history.
    auto snap = store_.snapshot();
    bool anyPanel = false;
    if (ImGui::BeginTabBar("##performtabs")) {
        for (auto const& cell : snap->cells) {
            if (cell->kind != CellKind::Panel) continue;
            anyPanel = true;
            std::string tab = (cell->name.empty() ? "(panel)" : cell->name)
                            + "###" + std::to_string(cell->id);
            if (ImGui::BeginTabItem(tab.c_str())) {
                ImGui::PushID((int)cell->id);
                auto& rt = runtime(cell->id, *cell);
                // The selected panel gets the full remaining height
                // (or its content, whichever is taller).
                float availH = ImGui::GetContentRegionAvail().y
                             / kPerformScale;
                float canvasH = std::max({availH, cell->panelHeight,
                                          rt.panelContentH});
                rt.panelContentH = drawPanelCanvas(
                    cell, ImGui::GetContentRegionAvail().x, ctx, controls,
                    false, kPerformScale, canvasH);
                ImGui::PopID();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    if (!anyPanel)
        ImGui::TextDisabled("(no panel cells in this notebook)");
    ImGui::EndChild();
}

void NotebookPanel::draw(float width, float height, GuiState& gui,
                         bridge::AppContext& ctx, ts::REPLSession* session,
                         ControlsPanel& controls) {
    if (!open_) return;

    // Perform mode toggle: Cmd+Shift+P anywhere (outside text input);
    // Esc leaves. The toolbar has matching buttons.
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiMod_Shift
                                     | ImGuiKey_P))
            performMode_ = !performMode_;
        if (performMode_ && ImGui::IsKeyPressed(ImGuiKey_Escape))
            performMode_ = false;
    }
    if (performMode_) {
        drawPerform(width, height, ctx, controls);
        return;
    }

    // Always show the vertical scrollbar: the cell strip must stay
    // navigable when cells overflow the view.
    ImGui::BeginChild("##notebook", ImVec2(width, height), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // Toolbar
    if (ImGui::SmallButton("+ code")) {
        int at = focusedCell_ ? store_.indexOf(focusedCell_) + 1
                              : store_.cellCount();
        focusedCell_ = store_.insertCell(at, CellKind::Code);
        structureCommitLabel_ = "add code cell";
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ prose")) {
        int at = focusedCell_ ? store_.indexOf(focusedCell_) + 1
                              : store_.cellCount();
        focusedCell_ = store_.insertCell(at, CellKind::Prose);
        structureCommitLabel_ = "add prose cell";
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ panel")) {
        int at = focusedCell_ ? store_.indexOf(focusedCell_) + 1
                              : store_.cellCount();
        static int panelCounter = 1;
        focusedCell_ = store_.insertCell(
            at, CellKind::Panel, "panel" + std::to_string(panelCounter++));
        structureCommitLabel_ = "add panel cell";
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ presets")) {
        int at = focusedCell_ ? store_.indexOf(focusedCell_) + 1
                              : store_.cellCount();
        focusedCell_ = store_.insertCell(at, CellKind::Presets);
        structureCommitLabel_ = "add presets cell";
    }
    ImGui::SameLine();
    if (!runQueue_.empty()) {
        // Queued runs in flight: the button becomes Stop with a count.
        char stopLabel[32];
        std::snprintf(stopLabel, sizeof(stopLabel), "Stop (%d)###runall",
                      (int)runQueue_.size());
        if (ImGui::SmallButton(stopLabel)) runQueue_.clear();
    } else if (ImGui::SmallButton("Run All###runall")) {
        runAll();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!store_.canUndo());
    if (ImGui::SmallButton("Undo")) {
        auto before = claimedPanels();
        applySnapshot(store_.undo(), ctx, before);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!store_.canRedo());
    if (ImGui::SmallButton("Redo")) {
        auto before = claimedPanels();
        applySnapshot(store_.redo(), ctx, before);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("History")) showHistory_ = !showHistory_;
    ImGui::SameLine();
    {
        std::string title = store_.filePath().empty()
                          ? std::string("(untitled notebook)")
                          : store_.filePath();
        if (modified()) title += " *";
        ImGui::TextDisabled("%s", title.c_str());
    }
    // Right side: Perform locks the layout and enlarges panel widgets
    // (Cmd+Shift+P / Esc); Editor swaps the view (notebook stays open;
    // Cmd+\ swaps back); Close ends the document.
    float rightW = ImGui::CalcTextSize("Perform").x
                 + ImGui::CalcTextSize("Editor").x
                 + ImGui::CalcTextSize("Close").x + 66.0f;
    ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - rightW, 0.0f));
    if (ImGui::SmallButton("Perform")) performMode_ = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Editor")) editorViewRequested_ = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Close")) closeRequested_ = true;
    ImGui::Separator();

    float cellWidth = ImGui::GetContentRegionAvail().x;
    auto snap = store_.snapshot();
    for (auto const& cell : snap->cells) {
        drawCell(cell, cellWidth, gui, ctx, session, controls);
    }

    // Apply structural edits requested during the cell loop.
    if (pendingDelete_) {
        runtime_.erase(pendingDelete_);
        store_.removeCell(pendingDelete_);
        if (focusedCell_ == pendingDelete_) focusedCell_ = 0;
        pendingDelete_ = 0;
        structureCommitLabel_ = "delete cell";
    }
    if (pendingMove_) {
        store_.moveCell(pendingMove_, pendingMoveDelta_);
        pendingMove_ = 0;
        structureCommitLabel_ = "move cell";
    }

    ImGui::EndChild();

    if (showHistory_) drawHistoryWindow(ctx);
}

// ---------------------------------------------------------------------------
// History window
// ---------------------------------------------------------------------------

void NotebookPanel::drawHistoryWindow(bridge::AppContext& ctx) {
    ImGui::SetNextWindowSize(ImVec2(280.0f, 360.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("History", &showHistory_)) {
        doc::HistNode* jump = nullptr;
        std::vector<doc::HistNode*> rows;   // nodes in draw (preorder) order
        int row = 0;
        // Depth-first over the whole tree; siblings indent under branches.
        std::function<void(doc::HistNode*, int)> walk =
            [&](doc::HistNode* n, int depth) {
                rows.push_back(n);
                ImGui::PushID(row++);
                ImGui::Indent((float)depth * 12.0f);
                std::string label = n->label;
                if (n->children.size() > 1) {
                    label += " (" + std::to_string(n->children.size())
                           + " branches)";
                }
                bool isCursor = (n == store_.historyCursor());
                if (isCursor && historyScrollPending_) {
                    ImGui::SetScrollHereY(0.5f);
                    historyScrollPending_ = false;
                }
                if (ImGui::Selectable(label.c_str(), isCursor)) {
                    jump = n;
                }
                ImGui::Unindent((float)depth * 12.0f);
                ImGui::PopID();
                for (auto& child : n->children) {
                    walk(child.get(),
                         depth + (n->children.size() > 1 ? 1 : 0));
                }
            };
        if (store_.historyRoot()) walk(store_.historyRoot(), 0);
        // While the window is focused, Up/Down step the cursor through the
        // rows exactly as listed (holding a key repeats): a linear history
        // scrubs like undo/redo, and crossing into a sibling branch is the
        // same jump a click performs. Each step recalls the state.
        if (ImGui::IsWindowFocused() && !rows.empty()) {
            int delta = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) delta = -1;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) delta = 1;
            if (delta != 0) {
                int cur = 0;
                for (int i = 0; i < (int)rows.size(); ++i) {
                    if (rows[i] == store_.historyCursor()) { cur = i; break; }
                }
                int next = std::clamp(cur + delta, 0, (int)rows.size() - 1);
                if (rows[next] != store_.historyCursor()) {
                    jump = rows[next];
                    historyScrollPending_ = true;
                }
            }
        }
        if (jump) {
            auto before = claimedPanels();
            applySnapshot(store_.jumpTo(jump), ctx, before);
        }
        ImGui::Separator();
        auto& pool = store_.interns();
        ImGui::TextDisabled("%d nodes | unique: %d cells, %d widget states, "
                            "%d presets",
                            row, pool.cells.liveCount(),
                            pool.snaps.liveCount(),
                            pool.presets.liveCount());
    }
    ImGui::End();
}
