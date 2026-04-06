// Tzopilotl
// Copyright (C) 2026 James McCartney

#include "find_replace.hpp"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstring>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string toLower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

static std::string::size_type findCaseInsensitive(const std::string& haystack,
                                                   const std::string& needle,
                                                   std::string::size_type pos) {
    auto it = std::search(haystack.begin() + pos, haystack.end(),
                          needle.begin(), needle.end(),
                          [](char a, char b) {
                              return std::tolower((unsigned char)a) ==
                                     std::tolower((unsigned char)b);
                          });
    if (it == haystack.end()) return std::string::npos;
    return (std::string::size_type)(it - haystack.begin());
}

// ---------------------------------------------------------------------------
// Show / hide
// ---------------------------------------------------------------------------

void FindReplaceState::show() {
    visible = true;
    focusFindField_ = true;
}

void FindReplaceState::hide() {
    visible = false;
}

void FindReplaceState::useSelectionForFind(const std::string& selection) {
    if (selection.empty()) return;
    size_t len = std::min(selection.size(), sizeof(findBuf) - 1);
    std::memcpy(findBuf, selection.c_str(), len);
    findBuf[len] = '\0';
    needsResearch_ = true;
}

void FindReplaceState::useSelectionForReplace(const std::string& selection) {
    if (selection.empty()) return;
    size_t len = std::min(selection.size(), sizeof(replaceBuf) - 1);
    std::memcpy(replaceBuf, selection.c_str(), len);
    replaceBuf[len] = '\0';
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

bool FindReplaceState::isWordBoundary(char c) const {
    return !std::isalnum((unsigned char)c) && c != '_';
}

void FindReplaceState::findAllMatches(const std::vector<std::string>& lines,
                                       int tabSize) {
    matches.clear();
    currentMatchIndex = -1;

    std::string needle(findBuf);
    if (needle.empty()) return;

    std::string needleLower = caseSensitive ? needle : toLower(needle);

    for (int lineNo = 0; lineNo < (int)lines.size(); ++lineNo) {
        const auto& lineText = lines[lineNo];
        std::string::size_type pos = 0;

        while (pos < lineText.size()) {
            std::string::size_type found;
            if (caseSensitive)
                found = lineText.find(needle, pos);
            else
                found = findCaseInsensitive(lineText, needleLower, pos);

            if (found == std::string::npos) break;

            std::string::size_type endPos = found + needle.size();

            // Check match mode constraints
            bool accept = true;
            switch (matchMode) {
                case FindMatchMode::Contains:
                    break;
                case FindMatchMode::MatchesWord:
                    if (found > 0 && !isWordBoundary(lineText[found - 1]))
                        accept = false;
                    if (endPos < lineText.size() && !isWordBoundary(lineText[endPos]))
                        accept = false;
                    break;
                case FindMatchMode::StartsWith:
                    if (found > 0 && !isWordBoundary(lineText[found - 1]))
                        accept = false;
                    break;
                case FindMatchMode::EndsWith:
                    if (endPos < lineText.size() && !isWordBoundary(lineText[endPos]))
                        accept = false;
                    break;
            }

            if (accept) {
                FindMatch m;
                int startCol = 0;
                for (std::string::size_type i = 0; i < found; ++i) {
                    if (lineText[i] == '\t')
                        startCol = (startCol / tabSize) * tabSize + tabSize;
                    else
                        ++startCol;
                }
                int endCol = startCol;
                for (std::string::size_type i = found; i < endPos; ++i) {
                    if (lineText[i] == '\t')
                        endCol = (endCol / tabSize) * tabSize + tabSize;
                    else
                        ++endCol;
                }
                m.start = TextEditor::Coordinates(lineNo, startCol);
                m.end = TextEditor::Coordinates(lineNo, endCol);
                matches.push_back(m);
            }

            pos = found + 1;
        }
    }
}

void FindReplaceState::search(TextEditor& editor) {
    auto cursor = editor.GetCursorPosition();
    findAllMatches(editor.GetTextLines(), editor.GetTabSize());
    needsResearch_ = false;

    if (matches.empty()) {
        currentMatchIndex = -1;
        return;
    }

    // Set current match to the one nearest (at or after) the cursor
    currentMatchIndex = 0;
    for (int i = 0; i < (int)matches.size(); ++i) {
        if (matches[i].start >= cursor) {
            currentMatchIndex = i;
            break;
        }
    }
}

void FindReplaceState::findNext(TextEditor& editor) {
    if (findBuf[0] == '\0') return;
    if (matches.empty()) {
        search(editor);
        if (matches.empty()) return;
    }

    currentMatchIndex = (currentMatchIndex + 1) % (int)matches.size();
    goToCurrentMatch(editor);
}

void FindReplaceState::findPrevious(TextEditor& editor) {
    if (findBuf[0] == '\0') return;
    if (matches.empty()) {
        search(editor);
        if (matches.empty()) return;
    }

    currentMatchIndex = (currentMatchIndex - 1 + (int)matches.size()) % (int)matches.size();
    goToCurrentMatch(editor);
}

void FindReplaceState::goToCurrentMatch(TextEditor& editor) {
    if (currentMatchIndex < 0 || currentMatchIndex >= (int)matches.size()) return;
    auto& m = matches[currentMatchIndex];
    editor.SetSelection(m.start, m.end);
    editor.SetCursorPosition(m.start);
}

void FindReplaceState::replaceCurrent(TextEditor& editor) {
    if (currentMatchIndex < 0 || currentMatchIndex >= (int)matches.size()) return;

    auto& m = matches[currentMatchIndex];
    editor.SetSelection(m.start, m.end);
    editor.DeleteSelection();
    editor.InsertText(replaceBuf);

    search(editor);
}

void FindReplaceState::replaceAll(TextEditor& editor) {
    if (matches.empty()) return;

    // Replace in reverse order to preserve earlier positions
    for (int i = (int)matches.size() - 1; i >= 0; --i) {
        editor.SetSelection(matches[i].start, matches[i].end);
        editor.DeleteSelection();
        editor.InsertText(replaceBuf);
    }

    search(editor);
}

// ---------------------------------------------------------------------------
// Highlight builder
// ---------------------------------------------------------------------------

std::vector<TextEditor::SearchHighlight> FindReplaceState::buildHighlights() const {
    std::vector<TextEditor::SearchHighlight> highlights;
    highlights.reserve(matches.size());
    for (int i = 0; i < (int)matches.size(); ++i) {
        TextEditor::SearchHighlight sh;
        sh.start = matches[i].start;
        sh.end = matches[i].end;
        sh.isCurrent = (i == currentMatchIndex);
        highlights.push_back(sh);
    }
    return highlights;
}

// ---------------------------------------------------------------------------
// Draw the find/replace bar
// ---------------------------------------------------------------------------

float FindReplaceState::drawBar(float panelWidth, TextEditor& editor) {
    float startY = ImGui::GetCursorPosY();
    bool highlightsChanged = false;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));

    // -- Find row --
    float fieldWidth = panelWidth * 0.45f;
    if (fieldWidth < 150.0f) fieldWidth = 150.0f;

    if (focusFindField_) {
        ImGui::SetKeyboardFocusHere();
        focusFindField_ = false;
    }

    ImGui::SetNextItemWidth(fieldWidth);
    bool findEnter = ImGui::InputText("##find", findBuf, sizeof(findBuf),
                                       ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited()) {
        search(editor);
        highlightsChanged = true;
    }
    if (findEnter && !matches.empty()) {
        findNext(editor);
        highlightsChanged = true;
    }

    ImGui::SameLine();

    // Case sensitivity toggle
    bool wasCaseSensitive = caseSensitive;
    if (wasCaseSensitive)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::SmallButton("Aa")) {
        caseSensitive = !caseSensitive;
        search(editor);
        highlightsChanged = true;
    }
    if (wasCaseSensitive)
        ImGui::PopStyleColor();

    ImGui::SameLine();

    // Match mode combo
    static const char* modeLabels[] = { "Contains", "Matches word", "Starts with", "Ends with" };
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::BeginCombo("##matchmode", modeLabels[(int)matchMode])) {
        for (int i = 0; i < 4; ++i) {
            bool selected = ((int)matchMode == i);
            if (ImGui::Selectable(modeLabels[i], selected)) {
                matchMode = (FindMatchMode)i;
                search(editor);
                highlightsChanged = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("<")) {
        findPrevious(editor);
        highlightsChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(">")) {
        findNext(editor);
        highlightsChanged = true;
    }

    ImGui::SameLine();

    // Match count display
    if (findBuf[0] != '\0') {
        if (matches.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No matches");
        } else {
            ImGui::Text("%d of %d", currentMatchIndex + 1, (int)matches.size());
        }
    }

    // Close button at right edge
    float closeX = panelWidth - 30.0f;
    ImGui::SameLine(closeX);
    if (ImGui::SmallButton("X")) {
        visible = false;
        editor.ClearSearchHighlights();
    }

    // -- Replace row --
    ImGui::SetNextItemWidth(fieldWidth);
    bool replaceEnter = ImGui::InputText("##replace", replaceBuf, sizeof(replaceBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();
    if (ImGui::SmallButton("Replace") || replaceEnter) {
        replaceCurrent(editor);
        highlightsChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Replace All")) {
        replaceAll(editor);
        highlightsChanged = true;
    }

    // Escape to dismiss
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && visible) {
        visible = false;
        editor.ClearSearchHighlights();
    }

    ImGui::PopStyleVar(2);

    if (highlightsChanged)
        editor.SetSearchHighlights(buildHighlights());

    float endY = ImGui::GetCursorPosY();
    return endY - startY;
}
