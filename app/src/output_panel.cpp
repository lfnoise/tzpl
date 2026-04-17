// Tzopilotl
// Copyright (C) 2026 James McCartney

#include "output_panel.hpp"
#include "imgui.h"

// Callback to apply pending cursor moves and track selection state
int OutputPanel::outputScrollCallback(ImGuiInputTextCallbackData* cbData) {
    auto* self = static_cast<OutputPanel*>(cbData->UserData);

    // Apply pending Cmd+Arrow cursor movement (set by moveTop/moveBottom/etc.)
    if (self->pendingMove_ != Move::None) {
        int newPos = cbData->CursorPos;
        switch (self->pendingMove_) {
            case Move::Top:    newPos = 0; break;
            case Move::Bottom: newPos = cbData->BufTextLen; break;
            case Move::Home:
                while (newPos > 0 && cbData->Buf[newPos - 1] != '\n') --newPos;
                break;
            case Move::End:
                while (newPos < cbData->BufTextLen && cbData->Buf[newPos] != '\n') ++newPos;
                break;
            default: break;
        }
        cbData->CursorPos = newPos;
        if (self->pendingMoveShift_)
            cbData->SelectionEnd = newPos;
        else
            cbData->SelectionStart = cbData->SelectionEnd = newPos;
        self->pendingMove_ = Move::None;
    }

    // Handle pending select-all from native menu Cmd+A
    if (self->pendingSelectAll_) {
        cbData->SelectionStart = 0;
        cbData->SelectionEnd = cbData->BufTextLen;
        self->pendingSelectAll_ = false;
    }

    // Track selection for external copy support (native menu Cmd+C)
    self->selStart_ = cbData->SelectionStart;
    self->selEnd_ = cbData->SelectionEnd;

    return 0;
}

bool OutputPanel::tryCopy() {
    if (!outputActive_ || selStart_ == selEnd_) return false;
    int s = std::min(selStart_, selEnd_);
    int e = std::max(selStart_, selEnd_);
    if (s < 0) s = 0;
    if (e > (int)outputText_.size()) e = (int)outputText_.size();
    std::string selected = outputText_.substr(s, e - s);
    ImGui::SetClipboardText(selected.c_str());
    return true;
}

bool OutputPanel::trySelectAll() {
    if (!outputActive_) return false;
    pendingSelectAll_ = true;
    return true;
}

void OutputPanel::clear(OutputBuffer& output) {
    output.clear();
    outputText_.clear();
    lastLineCount_ = 0;
    autoScroll_ = true;
}

void OutputPanel::draw(float width, float height, OutputBuffer& output) {
    ImGui::BeginChild("OutputPanel", ImVec2(width, height), false);

    // Drain any new lines
    output.drain();

    // Rebuild output text if line count changed
    size_t lineCount = output.lines().size();
    bool newOutput = (lineCount != lastLineCount_);
    if (newOutput) {
        lastLineCount_ = lineCount;
        outputText_.clear();
        for (auto& line : output.lines()) {
            if (line.kind == LineKind::Separator) {
                outputText_ += "----------------------------------------\n";
                continue;
            }
            switch (line.kind) {
                case LineKind::Result: outputText_ += "=> "; break;
                case LineKind::Error:  outputText_ += "!! "; break;
                case LineKind::Info:   outputText_ += "-- "; break;
                default: break;
            }
            outputText_ += line.text;
            outputText_ += '\n';
        }
    }

    // Scroll to bottom only as a one-shot: when new output arrives or after
    // clear().  Never force-scroll persistently -- that fights keyboard and
    // mouse-wheel navigation away from the bottom.
    bool scrollToBottom = newOutput || autoScroll_;
    autoScroll_ = false;

    if (scrollToBottom) {
        int numLines = 0;
        for (char c : outputText_) if (c == '\n') ++numLines;
        float lineHeight = ImGui::GetTextLineHeight();
        float expectedContentHeight = (numLines + 1) * lineHeight
                                      + ImGui::GetStyle().FramePadding.y * 2.0f;
        ImGui::SetNextWindowContentSize(ImVec2(0.0f, expectedContentHeight));
        ImGui::SetNextWindowScroll(ImVec2(-1.0f, FLT_MAX / 2));
    }

    // Selectable, copyable read-only text area
    ImGuiInputTextFlags outputFlags = ImGuiInputTextFlags_ReadOnly
                                    | ImGuiInputTextFlags_CallbackAlways;
    ImGui::InputTextMultiline("##output_text",
                              outputText_.data(), outputText_.size() + 1,
                              ImVec2(width, height),
                              outputFlags, outputScrollCallback, this);
    outputActive_ = ImGui::IsItemActive();

    ImGui::EndChild();
}
