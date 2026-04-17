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
    tabs_.push_back(std::move(tab));
    activeTab_ = (int)tabs_.size() - 1;
    needsFocus_ = true;
}

void EditorPanel::openFile(const std::string& path) {
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

    std::error_code ec;
    tab.diskWriteTime = fs::last_write_time(path, ec);

    tabs_.push_back(std::move(tab));
    activeTab_ = (int)tabs_.size() - 1;
    needsFocus_ = true;
}

void EditorPanel::closeActiveTab() {
    closeTab(activeTab_);
}

bool EditorPanel::save() {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return false;
    auto& tab = tabs_[activeTab_];
    if (tab.filePath.empty()) return false; // needs saveAs
    return saveAs(tab.filePath);
}

bool EditorPanel::saveAs(const std::string& path) {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return false;
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << tabs_[activeTab_].editor.GetText();
    file.close();

    auto& tab = tabs_[activeTab_];
    tab.filePath = path;
    tab.diskContent = tab.editor.GetText();
    tab.modified = false;
    // Update tab name from path
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
    tabs_.erase(tabs_.begin() + index);
    if (activeTab_ >= (int)tabs_.size())
        activeTab_ = std::max(0, (int)tabs_.size() - 1);
    if (tabs_.empty())
        newTab("scratch.x");
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

        int closeIdx = -1;
        for (int i = 0; i < (int)tabs_.size(); ++i) {
            bool open = true;
            // Show modified indicator
            std::string label = tabs_[i].name;
            if (tabs_[i].modified) label += " \xe2\x80\xa2";
            label += "###tab" + std::to_string(i);

            ImGuiTabItemFlags tabFlags = 0;
            if (i == pendingSelectTab_)
                tabFlags |= ImGuiTabItemFlags_SetSelected;

            if (ImGui::BeginTabItem(label.c_str(), &open, tabFlags)) {
                activeTab_ = i;
                ImGui::EndTabItem();
            }
            if (!open) closeIdx = i;
        }
        pendingSelectTab_ = -1;
        ImGui::EndTabBar();

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
