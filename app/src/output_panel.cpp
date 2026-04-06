// Tzopilotl
// Copyright (C) 2026 James McCartney

#include "output_panel.hpp"
#include "imgui.h"
#include <cstring>

OutputPanel::OutputPanel() {
    reclaimFocus_ = false;
}

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

    if (self->scrollToBottom_) {
        cbData->CursorPos = cbData->BufTextLen;
        cbData->SelectionStart = cbData->SelectionEnd = cbData->CursorPos;
        self->scrollToBottom_ = false;
    }
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
}

// ImGui InputText callback for command history navigation
int OutputPanel::inputCallback(ImGuiInputTextCallbackData* cbData) {
    auto* self = static_cast<OutputPanel*>(cbData->UserData);

    if (cbData->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        if (self->history_.empty()) return 0;

        if (cbData->EventKey == ImGuiKey_UpArrow) {
            if (self->historyIdx_ < 0)
                self->historyIdx_ = (int)self->history_.size() - 1;
            else if (self->historyIdx_ > 0)
                --self->historyIdx_;
        } else if (cbData->EventKey == ImGuiKey_DownArrow) {
            if (self->historyIdx_ >= 0) {
                ++self->historyIdx_;
                if (self->historyIdx_ >= (int)self->history_.size())
                    self->historyIdx_ = -1;
            }
        }

        const char* historyStr = (self->historyIdx_ >= 0)
            ? self->history_[self->historyIdx_].c_str()
            : "";
        cbData->DeleteChars(0, cbData->BufTextLen);
        cbData->InsertChars(0, historyStr);
    }
    return 0;
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
        scrollToBottom_ = true;
    }

    // Scrolling output area (leave room for input line)
    float inputHeight = ImGui::GetFrameHeightWithSpacing() + 4.0f;
    float outputHeight = height - inputHeight;

    // Selectable, copyable read-only text area
    ImGuiInputTextFlags outputFlags = ImGuiInputTextFlags_ReadOnly
                                    | ImGuiInputTextFlags_CallbackAlways;
    ImGui::InputTextMultiline("##output_text",
                              outputText_.data(), outputText_.size() + 1,
                              ImVec2(width, outputHeight),
                              outputFlags, outputScrollCallback, this);
    outputActive_ = ImGui::IsItemActive();

    // REPL input line
    ImGui::PushItemWidth(width - 8.0f);
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue
                              | ImGuiInputTextFlags_CallbackHistory;
    if (ImGui::InputText("##repl_input", inputBuf_, sizeof(inputBuf_),
                         flags, inputCallback, this)) {
        std::string input(inputBuf_);
        inputBuf_[0] = '\0';
        reclaimFocus_ = true;

        if (!input.empty()) {
            history_.push_back(input);
            historyIdx_ = -1;
            output.append("> " + input, LineKind::Info);
            pendingInput_ = input;
            scrollToBottom_ = true;
        }
    }
    ImGui::PopItemWidth();

    // Keep focus on input after submit
    if (reclaimFocus_) {
        ImGui::SetKeyboardFocusHere(-1);
        reclaimFocus_ = false;
    }

    ImGui::EndChild();
}
