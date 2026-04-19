// Tzopilotl
// Copyright (C) 2026 James McCartney

#include "editor_panel.hpp"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Tzopilotl language definition for syntax highlighting
// ---------------------------------------------------------------------------

TextEditor::LanguageDefinition EditorPanel::createTzopilotlDef() {
    TextEditor::LanguageDefinition def;
    def.mName = "Tzopilotl";

    // Keywords (control flow, declarations)
    const char* keywords[] = {
        "fn", "let", "var", "const", "if", "else", "while", "for",
        "break", "continue", "return", "case", "match",
        "struct", "enum", "import", "as", "where", "private",
        "coro", "yield", "constraint", "requires",
        "true", "false", "nil",
        "Int", "Float", "String", "Symbol", "Bool", "Void",
        "Fraction", "Complex", "Any", "type"
    };
    for (auto kw : keywords)
        def.mKeywords.insert(kw);

    // Comment markers
    def.mSingleLineComment = "--";
    def.mCommentStart = "/*";
    def.mCommentEnd = "*/";

    def.mAutoIndentation = true;
    def.mCaseSensitive = true;

    // Token regex patterns for numbers, strings, symbols
    // (PaletteIndex values: Number, String, CharLiteral, Punctuation, etc.)
    def.mTokenRegexStrings.push_back(
        std::make_pair<std::string, TextEditor::PaletteIndex>(
            "0[xX][0-9a-fA-F]+", TextEditor::PaletteIndex::Number));
    def.mTokenRegexStrings.push_back(
        std::make_pair<std::string, TextEditor::PaletteIndex>(
            "[0-9]+\\.?[0-9]*([eE][+-]?[0-9]+)?[i]?", TextEditor::PaletteIndex::Number));
    def.mTokenRegexStrings.push_back(
        std::make_pair<std::string, TextEditor::PaletteIndex>(
            "\\\"([^\\\"\\\\]|\\\\.)*\\\"", TextEditor::PaletteIndex::String));
    // Symbol literals: 'identifier
    def.mTokenRegexStrings.push_back(
        std::make_pair<std::string, TextEditor::PaletteIndex>(
            "'[a-zA-Z_][a-zA-Z0-9_]*", TextEditor::PaletteIndex::CharLiteral));
    // Identifiers
    def.mTokenRegexStrings.push_back(
        std::make_pair<std::string, TextEditor::PaletteIndex>(
            "[a-zA-Z_][a-zA-Z0-9_]*", TextEditor::PaletteIndex::Identifier));
    // Punctuation
    def.mTokenRegexStrings.push_back(
        std::make_pair<std::string, TextEditor::PaletteIndex>(
            "[\\[\\]\\{\\}\\(\\)\\<\\>\\=\\+\\-\\*\\/\\%\\^\\&\\|\\!\\~\\,\\.\\;\\:\\?\\@]",
            TextEditor::PaletteIndex::Punctuation));

    return def;
}

// ---------------------------------------------------------------------------
// EditorPanel
// ---------------------------------------------------------------------------

EditorPanel::EditorPanel()
    : langDef_(createTzopilotlDef())
{
    newTab("scratch.x");
}

void EditorPanel::newTab(const std::string& name) {
    Tab tab;
    tab.name = name;
    tab.id = nextTabId_++;
    tab.editorTitle = "##editor" + std::to_string(tab.id);
    tab.editor.SetLanguageDefinition(langDef_);
    tab.editor.SetShowWhitespaces(false);
    tab.diskContent = tab.editor.GetText();
    tab.ownerIdx = activeOwner_;
    tabs_.push_back(std::move(tab));
    activeTab_ = (int)tabs_.size() - 1;
    pendingSelectTab_ = activeTab_;
    needsFocus_ = true;
}

void EditorPanel::openFile(const std::string& path, int ownerIdx) {
    // If already open under any owner, just focus it. Caller is expected to
    // have called setActiveOwner() with the existing owner if they want it
    // surfaced in the current view.
    for (int i = 0; i < (int)tabs_.size(); ++i) {
        if (tabs_[i].filePath == path) {
            activeTab_ = i;
            pendingSelectTab_ = i;
            needsFocus_ = true;
            return;
        }
    }

    std::ifstream file(path);
    if (!file.is_open()) return;

    std::stringstream ss;
    ss << file.rdbuf();

    // Extract filename from path
    std::string name = path;
    auto slash = name.find_last_of('/');
    if (slash != std::string::npos)
        name = name.substr(slash + 1);

    Tab tab;
    tab.name = name;
    tab.filePath = path;
    tab.id = nextTabId_++;
    tab.editorTitle = "##editor" + std::to_string(tab.id);
    tab.editor.SetLanguageDefinition(langDef_);
    tab.editor.SetText(ss.str());
    tab.diskContent = tab.editor.GetText();  // use editor's canonical form
    tab.editor.SetShowWhitespaces(false);
    tab.ownerIdx = ownerIdx;

    std::error_code ec;
    tab.diskWriteTime = fs::last_write_time(path, ec);

    tabs_.push_back(std::move(tab));
    activeTab_ = (int)tabs_.size() - 1;
    pendingSelectTab_ = activeTab_;
    needsFocus_ = true;
}

int EditorPanel::findTabIdxById(unsigned id) const {
    for (int i = 0; i < (int)tabs_.size(); ++i)
        if (tabs_[i].id == id) return i;
    return -1;
}

int EditorPanel::firstTabOfOwner(int owner) const {
    for (int i = 0; i < (int)tabs_.size(); ++i)
        if (tabs_[i].ownerIdx == owner) return i;
    return -1;
}

void EditorPanel::setActiveOwner(int ownerIdx) {
    if (ownerIdx == activeOwner_) return;

    // Remember the active tab of the current owner so returning to it later
    // restores the same selection.
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size())
        lastActiveIdByOwner_[activeOwner_] = tabs_[activeTab_].id;

    activeOwner_ = ownerIdx;

    int newActive = -1;
    auto it = lastActiveIdByOwner_.find(ownerIdx);
    if (it != lastActiveIdByOwner_.end()) {
        int idx = findTabIdxById(it->second);
        if (idx >= 0 && tabs_[idx].ownerIdx == ownerIdx)
            newActive = idx;
    }
    if (newActive < 0)
        newActive = firstTabOfOwner(ownerIdx);

    activeTab_ = newActive;
    pendingSelectTab_ = newActive;
    needsFocus_ = true;
}

void EditorPanel::reassignOwnership(
    std::function<int(std::string const&)> computeOwner)
{
    for (auto& tab : tabs_) {
        if (!tab.filePath.empty())
            tab.ownerIdx = computeOwner(tab.filePath);
    }
    // If the active tab migrated out of the current view, select the first
    // remaining tab of the active owner (or nothing).
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()
        && tabs_[activeTab_].ownerIdx != activeOwner_) {
        activeTab_ = firstTabOfOwner(activeOwner_);
    }
}

std::optional<int> EditorPanel::findTabOwner(const std::string& path) const {
    for (auto const& tab : tabs_)
        if (tab.filePath == path) return tab.ownerIdx;
    return std::nullopt;
}

void EditorPanel::closeActiveTab() {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()
        && tabs_[activeTab_].modified) {
        pendingCloseTab_ = activeTab_;
        return;
    }
    closeTab(activeTab_);
}

bool EditorPanel::save() { return saveTab(activeTab_); }

bool EditorPanel::saveAs(const std::string& path) {
    return saveTabAs(activeTab_, path);
}

bool EditorPanel::saveTab(int idx) {
    if (idx < 0 || idx >= (int)tabs_.size()) return false;
    if (tabs_[idx].filePath.empty()) return false; // needs saveTabAs
    return saveTabAs(idx, tabs_[idx].filePath);
}

bool EditorPanel::saveTabAs(int idx, const std::string& path) {
    if (idx < 0 || idx >= (int)tabs_.size()) return false;
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << tabs_[idx].editor.GetText();
    file.close();

    auto& tab = tabs_[idx];
    tab.filePath = path;
    tab.diskContent = tab.editor.GetText();
    tab.modified = false;
    auto slash = path.find_last_of('/');
    tab.name = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    std::error_code ec;
    tab.diskWriteTime = fs::last_write_time(path, ec);

    return true;
}

bool EditorPanel::saveCopy(const std::string& path) {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return false;
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << tabs_[activeTab_].editor.GetText();
    return true;
}

bool EditorPanel::hasUnsavedChanges() const {
    for (auto& tab : tabs_)
        if (tab.modified) return true;
    return false;
}

std::vector<std::string> EditorPanel::unsavedFileNames() const {
    std::vector<std::string> names;
    for (auto& tab : tabs_)
        if (tab.modified) names.push_back(tab.name);
    return names;
}

int EditorPanel::saveAll() {
    int saved = 0;
    for (auto& tab : tabs_) {
        if (tab.modified && !tab.filePath.empty()) {
            std::ofstream file(tab.filePath);
            if (file.is_open()) {
                file << tab.editor.GetText();
                file.close();
                tab.diskContent = tab.editor.GetText();
                tab.modified = false;
                std::error_code ec;
                tab.diskWriteTime = fs::last_write_time(tab.filePath, ec);
                ++saved;
            }
        }
    }
    return saved;
}

bool EditorPanel::hasFilePath() const {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return false;
    return !tabs_[activeTab_].filePath.empty();
}

std::string EditorPanel::activeFilePath() const {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return "";
    return tabs_[activeTab_].filePath;
}

std::string EditorPanel::activeTabName() const {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return "";
    return tabs_[activeTab_].name;
}

std::string EditorPanel::pendingCloseName() const {
    if (pendingCloseTab_ < 0 || pendingCloseTab_ >= (int)tabs_.size()) return "";
    return tabs_[pendingCloseTab_].name;
}

bool EditorPanel::pendingCloseHasPath() const {
    if (pendingCloseTab_ < 0 || pendingCloseTab_ >= (int)tabs_.size()) return false;
    return !tabs_[pendingCloseTab_].filePath.empty();
}

bool EditorPanel::switchToFile(const std::string& path) {
    for (int i = 0; i < (int)tabs_.size(); ++i) {
        if (tabs_[i].filePath == path) {
            activeTab_ = i;
            pendingSelectTab_ = i;
            needsFocus_ = true;
            return true;
        }
    }
    return false;
}

void EditorPanel::closeTab(int index) {
    if (index < 0 || index >= (int)tabs_.size()) return;
    int closedOwner = tabs_[index].ownerIdx;
    tabs_.erase(tabs_.begin() + index);

    // Adjust activeTab_: if we removed it, pick another tab in the active
    // owner; otherwise just shift the index if we erased before it.
    if (activeTab_ == index) {
        activeTab_ = firstTabOfOwner(activeOwner_);
    } else if (activeTab_ > index) {
        --activeTab_;
    }

    // Ensure the Open Files view always has at least one tab (the scratch
    // tab). Workspace views are allowed to be empty -- the "+" button lets
    // the user add a new one.
    if (closedOwner == -1) {
        if (firstTabOfOwner(-1) < 0) {
            int saved = activeOwner_;
            activeOwner_ = -1;          // so newTab inherits -1 as owner
            newTab("scratch.x");
            activeOwner_ = saved;
            // If the user is currently viewing Open Files, select the new tab.
            if (activeOwner_ == -1)
                activeTab_ = (int)tabs_.size() - 1;
        }
    }
}

void EditorPanel::draw(float width, float height, GuiState& state) {
    checkForExternalChanges();

    ImGui::BeginChild("EditorPanel", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoScrollbar);

    // Tab bar
    if (ImGui::BeginTabBar("EditorTabs", ImGuiTabBarFlags_AutoSelectNewTabs
                                         | ImGuiTabBarFlags_FittingPolicyScroll)) {
        // "+" button to add new tab
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing
                                      | ImGuiTabItemFlags_NoTooltip)) {
            newTab();
        }

        // ImGui's SetSelected flag takes effect on the next frame, so on the
        // click frame BeginTabItem() may still return true for the
        // previously-selected tab. If we let that branch overwrite
        // activeTab_, the editor would render the stale tab for one frame
        // (and, if that was the remembered last-active tab of this owner,
        // the user sees the wrong file). Snapshot the pending request and
        // use it as the authoritative selection for this frame.
        int const pendingSelect = pendingSelectTab_;
        pendingSelectTab_ = -1;

        int closeIdx = -1;
        int selectedThisFrame = -1;
        for (int i = 0; i < (int)tabs_.size(); ++i) {
            // Only tabs matching the current owner (workspace view) are
            // drawn in the bar. Tabs in other owners remain live but hidden.
            if (tabs_[i].ownerIdx != activeOwner_) continue;

            bool open = true;
            // Show modified indicator
            std::string label = tabs_[i].name;
            if (tabs_[i].modified) label += " \xe2\x80\xa2";
            label += "###tab" + std::to_string(i);

            ImGuiTabItemFlags tabFlags = 0;
            if (i == pendingSelect)
                tabFlags |= ImGuiTabItemFlags_SetSelected;

            if (ImGui::BeginTabItem(label.c_str(), &open, tabFlags)) {
                selectedThisFrame = i;
                ImGui::EndTabItem();
            }
            if (!open) {
                // Modified tabs need save/discard/cancel confirmation;
                // defer to the app layer. Unmodified tabs close immediately.
                if (tabs_[i].modified) pendingCloseTab_ = i;
                else closeIdx = i;
            }
        }
        ImGui::EndTabBar();

        // Forced selection wins over whatever ImGui considered selected this
        // frame -- otherwise a one-frame mismatch between tab highlight and
        // rendered content occurs on cross-workspace file clicks.
        if (pendingSelect >= 0)
            activeTab_ = pendingSelect;
        else if (selectedThisFrame >= 0)
            activeTab_ = selectedThisFrame;

        if (closeIdx >= 0) closeTab(closeIdx);
    }

    // Find/replace bar (between tab bar and editor)
    if (findReplace_.visible && activeTab_ >= 0 && activeTab_ < (int)tabs_.size()) {
        findReplace_.drawBar(width, tabs_[activeTab_].editor);
    }

    // Editor content
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()) {
        auto& editor = tabs_[activeTab_].editor;

        // Give the editor ImGui focus once after tab switch or file open.
        // EnsureCursorVisible() sets mScrollToCursor which triggers
        // ImGui::SetWindowFocus() inside Render, giving the editor focus
        // so the cursor blinks.
        if (activeTab_ != prevActiveTab_ || needsFocus_) {
            prevActiveTab_ = activeTab_;
            needsFocus_ = false;
            editor.EnsureCursorVisible();
        }

        // Draw eval flash overlay
        if (state.flash.active()) {
            auto drawList = ImGui::GetWindowDrawList();
            auto cursorPos = ImGui::GetCursorScreenPos();
            float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            // Approximate: line numbers take ~50px, flash covers the editor area
            float x1 = cursorPos.x;
            float x2 = cursorPos.x + width;
            float y1 = cursorPos.y + state.flash.startLine * lineHeight;
            float y2 = cursorPos.y + (state.flash.endLine + 1) * lineHeight;
            ImU32 color = ImGui::ColorConvertFloat4ToU32(
                ImVec4(0.3f, 0.6f, 1.0f, state.flash.alpha * 0.3f));
            drawList->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), color);
        }

        editor.Render(tabs_[activeTab_].editorTitle.c_str());

        // pendingEdit_ catches menu-driven ops (undo/paste/indent/etc.)
        // that set mTextChanged before Render resets it.
        bool changed = pendingEdit_ || editor.IsTextChanged();
        pendingEdit_ = false;

        // Track modifications by comparing to on-disk content
        if (changed) {
            tabs_[activeTab_].modified =
                (editor.GetText() != tabs_[activeTab_].diskContent);
            // Refresh search highlights so they match the edited text
            updateSearchHighlights();
        }
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Text access
// ---------------------------------------------------------------------------

std::string EditorPanel::getSelectedText() const {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return "";
    return tabs_[activeTab_].editor.GetSelectedText();
}

std::string EditorPanel::getCurrentLineText() const {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return "";
    return tabs_[activeTab_].editor.GetCurrentLineText();
}

std::string EditorPanel::getAllText() const {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return "";
    return tabs_[activeTab_].editor.GetText();
}

TextEditor::Coordinates EditorPanel::getCursorPosition() const {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size())
        return TextEditor::Coordinates();
    return tabs_[activeTab_].editor.GetCursorPosition();
}

std::string EditorPanel::getCurrentBlockText(int& outStartLine, int& outEndLine) const {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) {
        outStartLine = outEndLine = 0;
        return "";
    }

    auto lines = tabs_[activeTab_].editor.GetTextLines();
    auto cursor = tabs_[activeTab_].editor.GetCursorPosition();
    int curLine = cursor.mLine;

    if (curLine >= (int)lines.size()) {
        outStartLine = outEndLine = 0;
        return "";
    }

    // Find block boundaries: contiguous non-empty lines around cursor
    outStartLine = curLine;
    while (outStartLine > 0) {
        const auto& line = lines[outStartLine - 1];
        bool empty = line.empty()
            || std::all_of(line.begin(), line.end(),
                           [](char c){ return c == ' ' || c == '\t'; });
        if (empty) break;
        --outStartLine;
    }

    outEndLine = curLine;
    while (outEndLine < (int)lines.size() - 1) {
        const auto& line = lines[outEndLine + 1];
        bool empty = line.empty()
            || std::all_of(line.begin(), line.end(),
                           [](char c){ return c == ' ' || c == '\t'; });
        if (empty) break;
        ++outEndLine;
    }

    // Build the block text
    std::string result;
    for (int i = outStartLine; i <= outEndLine; ++i) {
        if (i > outStartLine) result += '\n';
        result += lines[i];
    }
    return result;
}

// ---------------------------------------------------------------------------
// Edit operations
// ---------------------------------------------------------------------------

void EditorPanel::cut() {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()) {
        tabs_[activeTab_].editor.Cut();
        pendingEdit_ = true;
    }
}

void EditorPanel::copy() {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size())
        tabs_[activeTab_].editor.Copy();
}

void EditorPanel::paste() {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()) {
        tabs_[activeTab_].editor.Paste();
        pendingEdit_ = true;
    }
}

void EditorPanel::undo() {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()) {
        tabs_[activeTab_].editor.Undo();
        pendingEdit_ = true;
    }
}

void EditorPanel::redo() {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()) {
        tabs_[activeTab_].editor.Redo();
        pendingEdit_ = true;
    }
}

void EditorPanel::selectAll() {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size())
        tabs_[activeTab_].editor.SelectAll();
}

void EditorPanel::toggleComment() {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()) {
        tabs_[activeTab_].editor.ToggleLineComment();
        pendingEdit_ = true;
    }
}

void EditorPanel::indent() {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()) {
        tabs_[activeTab_].editor.IndentLines();
        pendingEdit_ = true;
    }
}

void EditorPanel::outdent() {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()) {
        tabs_[activeTab_].editor.OutdentLines();
        pendingEdit_ = true;
    }
}

void EditorPanel::moveHome(bool select) {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size())
        tabs_[activeTab_].editor.MoveHome(select);
}

void EditorPanel::moveEnd(bool select) {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size())
        tabs_[activeTab_].editor.MoveEnd(select);
}

void EditorPanel::moveTop(bool select) {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size())
        tabs_[activeTab_].editor.MoveTop(select);
}

void EditorPanel::moveBottom(bool select) {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size())
        tabs_[activeTab_].editor.MoveBottom(select);
}

// ---------------------------------------------------------------------------
// Error markers
// ---------------------------------------------------------------------------

void EditorPanel::setErrorMarkers(const TextEditor::ErrorMarkers& markers) {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size())
        tabs_[activeTab_].editor.SetErrorMarkers(markers);
}

void EditorPanel::clearErrorMarkers() {
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()) {
        TextEditor::ErrorMarkers empty;
        tabs_[activeTab_].editor.SetErrorMarkers(empty);
    }
}

TextEditor* EditorPanel::activeEditor() {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return nullptr;
    return &tabs_[activeTab_].editor;
}

// ---------------------------------------------------------------------------
// External file change detection
// ---------------------------------------------------------------------------

void EditorPanel::checkForExternalChanges() {
    using namespace std::chrono;
    auto now = steady_clock::now();
    if (now - lastDiskCheck_ < 1s)
        return;
    lastDiskCheck_ = now;

    for (auto& tab : tabs_) {
        if (tab.filePath.empty()) continue;

        std::error_code ec;
        auto diskTime = fs::last_write_time(tab.filePath, ec);
        if (ec) continue; // file gone or unreadable -- ignore

        if (diskTime != tab.diskWriteTime) {
            tab.diskWriteTime = diskTime;

            // If the user has unsaved edits, don't clobber them
            if (tab.modified) continue;

            std::ifstream file(tab.filePath);
            if (!file.is_open()) continue;

            std::stringstream ss;
            ss << file.rdbuf();

            // Preserve cursor position across reload
            auto cursor = tab.editor.GetCursorPosition();
            tab.editor.SetText(ss.str());
            tab.diskContent = tab.editor.GetText();
            tab.editor.SetCursorPosition(cursor);
            tab.modified = false;
        }
    }
}

void EditorPanel::updateSearchHighlights() {
    auto* ed = activeEditor();
    if (!ed) return;
    if (findReplace_.visible && findReplace_.findBuf[0] != '\0') {
        findReplace_.search(*ed);  // re-search against current text
        ed->SetSearchHighlights(findReplace_.buildHighlights());
    } else {
        ed->ClearSearchHighlights();
    }
}
