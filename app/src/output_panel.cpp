// Tzopilotl
// Copyright (C) 2026 James McCartney

#include "output_panel.hpp"
#include "imgui.h"
#include "imgui_internal.h"

// Callback to auto-scroll output and track selection state
int OutputPanel::outputScrollCallback(ImGuiInputTextCallbackData* cbData) {
    auto* self = static_cast<OutputPanel*>(cbData->UserData);
    ImGuiIO& io = ImGui::GetIO();

    // macOS: Cmd+Arrow for line/text start/end
    // ImGui's InputTextMultiline doesn't handle these correctly on macOS.
    if (io.ConfigMacOSXBehaviors && io.KeySuper && !io.KeyCtrl && !io.KeyAlt) {
        bool shift = io.KeyShift;
        int newPos = cbData->CursorPos;
        bool handled = false;

        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            // Move to beginning of current line (undo ImGui's char-left first)
            int origPos = cbData->CursorPos + 1;
            if (origPos > cbData->BufTextLen) origPos = cbData->BufTextLen;
            newPos = origPos;
            while (newPos > 0 && cbData->Buf[newPos - 1] != '\n')
                --newPos;
            handled = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            // Move to end of current line (undo ImGui's char-right first)
            int origPos = cbData->CursorPos > 0 ? cbData->CursorPos - 1 : 0;
            newPos = origPos;
            while (newPos < cbData->BufTextLen && cbData->Buf[newPos] != '\n')
                ++newPos;
            handled = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            newPos = 0;
            handled = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            newPos = cbData->BufTextLen;
            handled = true;
        }

        if (handled) {
            cbData->CursorPos = newPos;
            if (shift) {
                cbData->SelectionEnd = newPos;
            } else {
                cbData->SelectionStart = cbData->SelectionEnd = newPos;
            }
        }
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
    if (lineCount != lastLineCount_) {
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

    // Compute the child window name that InputTextMultiline will create
    ImGuiWindow* parentWin = ImGui::GetCurrentWindow();
    ImGuiID textId = parentWin->GetID("##output_text");
    char childName[256];
    snprintf(childName, sizeof(childName), "%s/##output_text_%08X", parentWin->Name, textId);

    // Find the text child window (set up during a previous frame). Check if the
    // user's mouse wheel scrolled it away from the bottom -- NewFrame()'s
    // UpdateMouseWheel() sets ScrollTarget before draw() is called. We must detect
    // this BEFORE calling SetNextWindowScroll, which would overwrite the user's
    // scroll target.
    ImGuiWindow* textWindow = ImGui::FindWindowByName(childName);
    if (textWindow && textWindow->ScrollTarget.y < FLT_MAX
        && textWindow->ScrollTarget.y < textWindow->Scroll.y) {
        // User scrolled up (target is above current position)
        autoScroll_ = false;
    }

    // When auto-scrolling, override content size and scroll BEFORE InputTextMultiline
    // so the scroll is applied on the same frame with correct ScrollMax (avoiding
    // one-frame lag that would otherwise show the scroll catching up on the next frame).
    if (autoScroll_) {
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

    // Re-enable auto-scroll when user scrolls back to the bottom
    textWindow = ImGui::FindWindowByName(childName);
    if (textWindow && !autoScroll_ && textWindow->ScrollMax.y > 0.0f) {
        if (textWindow->Scroll.y >= textWindow->ScrollMax.y - ImGui::GetTextLineHeight())
            autoScroll_ = true;
    }

    ImGui::EndChild();
}
