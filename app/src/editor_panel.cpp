// Tzopilotl
// Copyright (C) 2026 James McCartney

#include "editor_panel.hpp"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <algorithm>

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
    tab.editor.SetLanguageDefinition(langDef_);
    tab.editor.SetShowWhitespaces(false);
    tabs_.push_back(std::move(tab));
    activeTab_ = (int)tabs_.size() - 1;
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
    tab.editor.SetLanguageDefinition(langDef_);
    tab.editor.SetText(ss.str());
    tab.editor.SetShowWhitespaces(false);
    tabs_.push_back(std::move(tab));
    activeTab_ = (int)tabs_.size() - 1;
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
    tab.modified = false;
    // Update tab name from path
    auto slash = path.find_last_of('/');
    tab.name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
    return true;
}

bool EditorPanel::saveCopy(const std::string& path) {
    if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return false;
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << tabs_[activeTab_].editor.GetText();
    return true;
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

void EditorPanel::closeTab(int index) {
    if (index < 0 || index >= (int)tabs_.size()) return;
    tabs_.erase(tabs_.begin() + index);
    if (activeTab_ >= (int)tabs_.size())
        activeTab_ = std::max(0, (int)tabs_.size() - 1);
    if (tabs_.empty())
        newTab("scratch.x");
}

void EditorPanel::draw(float width, float height, GuiState& state) {
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
            if (tabs_[i].modified) label += " *";
            label += "###tab" + std::to_string(i);

            if (ImGui::BeginTabItem(label.c_str(), &open)) {
                activeTab_ = i;
                ImGui::EndTabItem();
            }
            if (!open) closeIdx = i;
        }
        ImGui::EndTabBar();

        if (closeIdx >= 0) closeTab(closeIdx);
    }

    // Editor content
    if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()) {
        auto& editor = tabs_[activeTab_].editor;

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

        editor.Render("##editor");

        // Track modifications
        tabs_[activeTab_].modified = editor.IsTextChanged();
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
