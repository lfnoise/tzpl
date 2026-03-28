// Tzopilotl
// Copyright (C) 2026 James McCartney

#include "output_panel.hpp"
#include "imgui.h"
#include "repl_session.hpp"
#include "nrt_vm.hpp"
#include "diagnostic.hpp"
#include <mutex>
#include <cstring>

using namespace ts;

OutputPanel::OutputPanel() {
    reclaimFocus_ = false;
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

void OutputPanel::draw(float width, float height, OutputBuffer& output,
                       REPLSession* session, NRTVM* nrtvm) {
    ImGui::BeginChild("OutputPanel", ImVec2(width, height), false);

    // Drain any new lines
    output.drain();

    // Scrolling output area (leave room for input line)
    float inputHeight = ImGui::GetFrameHeightWithSpacing() + 4.0f;
    float outputHeight = height - inputHeight;

    ImGui::BeginChild("OutputScroll", ImVec2(width, outputHeight), true);
    for (auto& line : output.lines()) {
        ImVec4 color;
        switch (line.kind) {
            case LineKind::Output: color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); break;
            case LineKind::Result: color = ImVec4(0.4f, 0.9f, 0.5f, 1.0f); break;
            case LineKind::Error:  color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break;
            case LineKind::Info:   color = ImVec4(0.6f, 0.7f, 0.9f, 1.0f); break;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", line.text.c_str());
        ImGui::PopStyleColor();
    }
    if (scrollToBottom_) {
        ImGui::SetScrollHereY(1.0f);
        scrollToBottom_ = false;
    }
    ImGui::EndChild();

    // REPL input line
    ImGui::PushItemWidth(width - 8.0f);
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue
                              | ImGuiInputTextFlags_CallbackHistory;
    if (ImGui::InputText("##repl_input", inputBuf_, sizeof(inputBuf_),
                         flags, inputCallback, this)) {
        std::string input(inputBuf_);
        inputBuf_[0] = '\0';
        reclaimFocus_ = true;

        if (!input.empty() && session && nrtvm) {
            // Add to history
            history_.push_back(input);
            historyIdx_ = -1;

            // Evaluate
            std::lock_guard<std::mutex> lock(nrtvm->mtx);
            nrtvm->vm.makeCurrent();

            auto result = session->eval(input);

            if (!result.errors.empty()) {
                auto formatted = formatErrorsPlain(result.errors, input, "<repl>");
                for (auto& line : formatted) {
                    output.append(line, LineKind::Error);
                }
            } else if (result.hasValue) {
                output.append("\xe2\x86\x92 " + result.formattedValue
                              + " : " + result.typeName, LineKind::Result);
            }

            nrtvm->vm.gcHeartbeat();
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
