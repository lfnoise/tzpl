// Tzopilotl
// Copyright (C) 2026 James McCartney

#ifndef find_replace_hpp
#define find_replace_hpp

#include "TextEditor.h"
#include <string>
#include <vector>

enum class FindMatchMode {
    Contains,
    MatchesWord,
    StartsWith,
    EndsWith
};

struct FindMatch {
    TextEditor::Coordinates start;
    TextEditor::Coordinates end;
};

class FindReplaceState {
public:
    bool visible = false;
    char findBuf[1024] = {};
    char replaceBuf[1024] = {};
    bool caseSensitive = false;
    FindMatchMode matchMode = FindMatchMode::Contains;

    std::vector<FindMatch> matches;
    int currentMatchIndex = -1;

    void show();
    void hide();

    void useSelectionForFind(const std::string& selection);
    void useSelectionForReplace(const std::string& selection);

    void search(TextEditor& editor);
    void findNext(TextEditor& editor);
    void findPrevious(TextEditor& editor);
    void replaceCurrent(TextEditor& editor);
    void replaceAll(TextEditor& editor);

    // Draw the find/replace bar. Returns the height consumed.
    // Handles prev/next/replace actions internally.
    float drawBar(float panelWidth, TextEditor& editor);

    std::vector<TextEditor::SearchHighlight> buildHighlights() const;

    bool searchParamsChanged() const { return needsResearch_; }
    void clearSearchChanged() { needsResearch_ = false; }

private:
    void findAllMatches(const std::vector<std::string>& lines, int tabSize);
    void goToCurrentMatch(TextEditor& editor);
    bool isWordBoundary(char c) const;

    bool needsResearch_ = false;
    bool focusFindField_ = false;
    bool selectAllPending_ = false;
};

#endif
