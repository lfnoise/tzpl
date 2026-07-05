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

#include "widget_draw.hpp"

#include "tzpl_ui_state.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// All widget interaction happens with UIState::mtx held (GUI thread only).
// Never call into the VM from here (lock order: see tzpl_ui_state.hpp).

using bridge::UIWidget;
using bridge::UIWidgetKind;

static void markDirty(UIWidget& w) {
    w.dirtyEngine = true;
    w.dirtyCallback = true;
}

// One history commit per continuous interaction: flag the release edge of
// the last-drawn item (sliders, drags, checkboxes).
static void markGestureEdges(UIWidget& w) {
    if (ImGui::IsItemDeactivatedAfterEdit()) w.gestureEnded = true;
}

static void drawSlider(UIWidget& w) {
    float pos = static_cast<float>(w.spec.unmap(w.values[0]));
    char display[64];
    std::snprintf(display, sizeof(display), "%.4g", w.values[0]);
    ImGui::SetNextItemWidth(w.fw > 0.0f ? w.fw : -140.0f);
    if (ImGui::SliderFloat((std::string("##") + w.name).c_str(), &pos,
                           0.0f, 1.0f, display)) {
        w.values[0] = w.spec.map(pos);
        markDirty(w);
    }
    markGestureEdges(w);
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawNumber(UIWidget& w) {
    double v = w.values[0];
    ImGui::SetNextItemWidth(w.fw > 0.0f ? w.fw : -140.0f);
    if (ImGui::DragScalar((std::string("##") + w.name).c_str(),
                          ImGuiDataType_Double, &v, 0.01f, nullptr, nullptr,
                          "%.4g")) {
        w.values[0] = w.spec.clamp(v);
        markDirty(w);
    }
    markGestureEdges(w);
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawButton(UIWidget& w) {
    // Momentary/gate: 1 while held, 0 on release; both edges dispatch.
    ImGui::Button(w.name.c_str(), ImVec2(120.0f, 0.0f));
    if (ImGui::IsItemActivated()) {
        w.values[0] = 1.0;
        markDirty(w);
    }
    if (ImGui::IsItemDeactivated()) {
        w.values[0] = 0.0;
        markDirty(w);
    }
}

static void drawToggle(UIWidget& w) {
    bool on = w.values[0] != 0.0;
    if (ImGui::Checkbox(w.name.c_str(), &on)) {
        w.values[0] = on ? 1.0 : 0.0;
        markDirty(w);
    }
    markGestureEdges(w);
}

static float ampToDbPos(float amp) {
    // Map amplitude to a 0..1 meter position on a -60..0 dB scale.
    if (amp <= 0.001f) return 0.0f;
    float db = 20.0f * std::log10(amp);
    return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
}

static void drawMeter(UIWidget& w) {
    float rms = w.values.size() > 0 ? static_cast<float>(w.values[0]) : 0.0f;
    float peak = w.values.size() > 1 ? static_cast<float>(w.values[1]) : 0.0f;

    const float width = w.fw > 0.0f ? w.fw : 200.0f;
    const float height = w.fh > 0.0f ? w.fh : 14.0f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(width, height));
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(ImGuiCol_FrameBg));
    float rmsW = ampToDbPos(rms) * width;
    dl->AddRectFilled(origin, ImVec2(origin.x + rmsW, origin.y + height),
                      ImGui::GetColorU32(ImGuiCol_PlotHistogram));
    float peakX = origin.x + ampToDbPos(peak) * width;
    ImU32 peakCol = peak >= 1.0f ? IM_COL32(255, 64, 64, 255)
                                 : ImGui::GetColorU32(ImGuiCol_SliderGrab);
    dl->AddLine(ImVec2(peakX, origin.y), ImVec2(peakX, origin.y + height),
                peakCol, 2.0f);
    dl->AddRect(origin, ImVec2(origin.x + width, origin.y + height),
                ImGui::GetColorU32(ImGuiCol_Border));

    ImGui::SameLine();
    float db = peak > 0.001f ? 20.0f * std::log10(peak) : -60.0f;
    ImGui::Text("%s  %5.1f dB", w.name.c_str(), db);
}

static void drawScope(UIWidget& w) {
    const int displayN = 512;
    int chans = std::max(1, w.scopeChans);
    if (w.scopeChannel >= chans) w.scopeChannel = -1;

    // The ring holds interleaved frames; de-interleave the tail.
    int frames = (int)(w.scopeRing.size() / chans);

    // Trigger on a rising zero crossing of the reference channel (the
    // selected one, or channel 0 in "all" view) so periodic signals hold
    // still; all channels share the trigger to keep phase relationships.
    int refCh = w.scopeChannel < 0 ? 0 : w.scopeChannel;
    auto sample = [&](int frame, int ch) {
        return w.scopeRing[(size_t)frame * chans + ch];
    };
    int start = std::max(0, frames - displayN);
    for (int f = std::max(0, frames - 2 * displayN); f < frames - displayN; ++f) {
        if (sample(f, refCh) <= 0.0f && sample(f + 1, refCh) > 0.0f) {
            start = f;
            break;
        }
    }
    int count = std::min(displayN, frames - start);

    static std::vector<float> lane;  // GUI thread only
    auto plotLane = [&](int ch, float height) {
        lane.resize((size_t)std::max(count, 0));
        for (int f = 0; f < count; ++f) lane[f] = sample(start + f, ch);
        float scopeW = w.fw > 0.0f ? w.fw : 320.0f;
        if (count > 1) {
            ImGui::PlotLines(("##" + w.name + std::to_string(ch)).c_str(),
                             lane.data(), count, 0, nullptr,
                             -1.0f, 1.0f, ImVec2(scopeW, height));
        } else {
            ImGui::Dummy(ImVec2(scopeW, height));
        }
    };

    float scopeH = w.fh > 0.0f ? w.fh : 100.0f;
    ImGui::BeginGroup();
    if (w.scopeChannel < 0) {
        // All channels, stacked lanes sharing one trigger.
        float laneH = std::max(scopeH / (float)chans, 24.0f);
        for (int ch = 0; ch < chans; ++ch) plotLane(ch, laneH);
    } else {
        plotLane(w.scopeChannel, scopeH);
    }
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(w.name.c_str());
    if (chans > 1) {
        // Cycle: all -> 0 -> 1 -> ... -> all
        char label[16];
        if (w.scopeChannel < 0) std::snprintf(label, sizeof(label), "ch: all");
        else std::snprintf(label, sizeof(label), "ch: %d", w.scopeChannel);
        if (ImGui::SmallButton(label)) {
            w.scopeChannel = (w.scopeChannel + 1 < chans) ? w.scopeChannel + 1
                                                          : -1;
        }
    }
    ImGui::EndGroup();
}

static void drawPlot(UIWidget& w) {
    if (w.plotData.size() > 1) {
        float lo = w.plotData[0], hi = w.plotData[0];
        for (float v : w.plotData) {
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        if (hi == lo) { hi += 1.0f; lo -= 1.0f; }
        float pad = (hi - lo) * 0.05f;
        ImGui::PlotLines((std::string("##") + w.name).c_str(),
                         w.plotData.data(), (int)w.plotData.size(), 0, nullptr,
                         lo - pad, hi + pad,
                         ImVec2(w.fw > 0.0f ? w.fw : 320.0f,
                                w.fh > 0.0f ? w.fh : 100.0f));
    } else {
        ImGui::Dummy(ImVec2(w.fw > 0.0f ? w.fw : 320.0f,
                            w.fh > 0.0f ? w.fh : 100.0f));
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawWaveform(UIWidget& w) {
    const float width = w.fw > 0.0f ? w.fw : 320.0f;
    const float height = w.fh > 0.0f ? w.fh : 80.0f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(width, height));
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(ImGuiCol_FrameBg));
    int bins = (int)w.waveMin.size();
    if (bins > 0) {
        float midY = origin.y + height * 0.5f;
        float halfH = height * 0.5f;
        ImU32 col = ImGui::GetColorU32(ImGuiCol_PlotLines);
        for (int px = 0; px < (int)width; ++px) {
            int b0 = bins * px / (int)width;
            int b1 = bins * (px + 1) / (int)width;
            if (b1 <= b0) b1 = b0 + 1;
            float lo = w.waveMin[b0], hi = w.waveMax[b0];
            for (int b = b0 + 1; b < b1 && b < bins; ++b) {
                lo = std::min(lo, w.waveMin[b]);
                hi = std::max(hi, w.waveMax[b]);
            }
            float x = origin.x + px;
            dl->AddLine(ImVec2(x, midY - hi * halfH),
                        ImVec2(x, midY - lo * halfH), col);
        }
        dl->AddLine(ImVec2(origin.x, midY), ImVec2(origin.x + width, midY),
                    ImGui::GetColorU32(ImGuiCol_Border));
    }
    dl->AddRect(origin, ImVec2(origin.x + width, origin.y + height),
                ImGui::GetColorU32(ImGuiCol_Border));
    ImGui::Text("%s  (%lld frames)", w.name.c_str(),
                (long long)w.waveFrames);
}

static void drawXY(UIWidget& w) {
    const float size = w.fw > 0.0f ? w.fw : 160.0f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(size, size));
    bool active = ImGui::IsItemActive();
    if (active) {
        ImVec2 m = ImGui::GetIO().MousePos;
        float px = std::clamp((m.x - origin.x) / size, 0.0f, 1.0f);
        float py = 1.0f - std::clamp((m.y - origin.y) / size, 0.0f, 1.0f);
        w.values[0] = w.spec.map(px);
        w.values[1] = w.spec2.map(py);
        markDirty(w);
    }
    if (active) {
        w.gestureActive = true;
    } else if (w.gestureActive) {
        w.gestureActive = false;
        w.gestureEnded = true;
    }
    // Pad frame + crosshair
    auto* dl = ImGui::GetWindowDrawList();
    ImU32 frameCol = ImGui::GetColorU32(active ? ImGuiCol_FrameBgActive
                                               : ImGuiCol_FrameBg);
    dl->AddRectFilled(origin, ImVec2(origin.x + size, origin.y + size), frameCol);
    dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size),
                ImGui::GetColorU32(ImGuiCol_Border));
    float cx = origin.x + static_cast<float>(w.spec.unmap(w.values[0])) * size;
    float cy = origin.y + (1.0f - static_cast<float>(w.spec2.unmap(w.values[1]))) * size;
    ImU32 cursorCol = ImGui::GetColorU32(ImGuiCol_SliderGrab);
    dl->AddLine(ImVec2(origin.x, cy), ImVec2(origin.x + size, cy), cursorCol);
    dl->AddLine(ImVec2(cx, origin.y), ImVec2(cx, origin.y + size), cursorCol);
    dl->AddCircleFilled(ImVec2(cx, cy), 4.0f, cursorCol);

    ImGui::Text("%s  (%.4g, %.4g)", w.name.c_str(), w.values[0], w.values[1]);
}

static void drawMultiSlider(UIWidget& w) {
    int n = (int)w.values.size();
    if (n < 1) return;
    const float width = w.fw > 0.0f ? w.fw : 240.0f;
    const float height = w.fh > 0.0f ? w.fh : 80.0f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(width, height));
    bool active = ImGui::IsItemActive();
    if (active) {
        ImVec2 m = ImGui::GetIO().MousePos;
        int idx = std::clamp((int)((m.x - origin.x) / width * n), 0, n - 1);
        float pos = 1.0f - std::clamp((m.y - origin.y) / height, 0.0f, 1.0f);
        w.values[(size_t)idx] = w.spec.map(pos);
        markDirty(w);
    }
    if (active) {
        w.gestureActive = true;
    } else if (w.gestureActive) {
        w.gestureActive = false;
        w.gestureEnded = true;
    }
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(ImGuiCol_FrameBg));
    ImU32 barCol = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    float barW = width / (float)n;
    for (int i = 0; i < n; ++i) {
        float p = (float)w.spec.unmap(w.values[(size_t)i]);
        float x0 = origin.x + i * barW;
        dl->AddRectFilled(ImVec2(x0 + 1.0f, origin.y + (1.0f - p) * height),
                          ImVec2(x0 + barW - 1.0f, origin.y + height), barCol);
    }
    dl->AddRect(origin, ImVec2(origin.x + width, origin.y + height),
                ImGui::GetColorU32(ImGuiCol_Border));
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawMatrix(UIWidget& w) {
    int rows = std::max(1, w.rows), cols = std::max(1, w.cols);
    const float width = w.fw > 0.0f ? w.fw : cols * 22.0f;
    const float height = w.fh > 0.0f ? w.fh : rows * 22.0f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(width, height));
    if (ImGui::IsItemClicked()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        int c = std::clamp((int)((m.x - origin.x) / width * cols), 0, cols - 1);
        int r = std::clamp((int)((m.y - origin.y) / height * rows), 0, rows - 1);
        size_t i = (size_t)r * cols + c;
        if (i < w.values.size()) {
            w.values[i] = w.values[i] > 0.5 ? 0.0 : 1.0;
            markDirty(w);
            w.gestureEnded = true;  // one history commit per toggle
        }
    }
    auto* dl = ImGui::GetWindowDrawList();
    float cw = width / (float)cols, chh = height / (float)rows;
    ImU32 onCol = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    ImU32 gridCol = ImGui::GetColorU32(ImGuiCol_Border);
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(ImGuiCol_FrameBg));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            size_t i = (size_t)r * cols + c;
            if (i < w.values.size() && w.values[i] > 0.5) {
                dl->AddRectFilled(
                    ImVec2(origin.x + c * cw + 1, origin.y + r * chh + 1),
                    ImVec2(origin.x + (c + 1) * cw - 1,
                           origin.y + (r + 1) * chh - 1), onCol);
            }
        }
    }
    for (int c = 0; c <= cols; ++c)
        dl->AddLine(ImVec2(origin.x + c * cw, origin.y),
                    ImVec2(origin.x + c * cw, origin.y + height), gridCol);
    for (int r = 0; r <= rows; ++r)
        dl->AddLine(ImVec2(origin.x, origin.y + r * chh),
                    ImVec2(origin.x + width, origin.y + r * chh), gridCol);
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawPianoRoll(UIWidget& w) {
    const float stepBeats = 0.25f;  // 16th grid
    int rows = std::max(1, w.rollRows);
    int cols = std::max(1, (int)std::lround(w.rollBeats / stepBeats));
    const float width = w.fw > 0.0f ? w.fw : 320.0f;
    const float height = w.fh > 0.0f ? w.fh : 200.0f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(width, height));
    float cw = width / (float)cols, chh = height / (float)rows;

    if (ImGui::IsItemClicked()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        int c = std::clamp((int)((m.x - origin.x) / cw), 0, cols - 1);
        int r = std::clamp((int)((m.y - origin.y) / chh), 0, rows - 1);
        float pitch = (float)(w.rollLowPitch + (rows - 1 - r));
        float start = c * stepBeats;
        // Click a note to delete it; click empty space to add a 16th note.
        bool removed = false;
        for (size_t i = 0; i + 2 < w.noteData.size(); i += 3) {
            if (w.noteData[i] == pitch && start >= w.noteData[i + 1]
                && start < w.noteData[i + 1] + w.noteData[i + 2]) {
                w.noteData.erase(w.noteData.begin() + i,
                                 w.noteData.begin() + i + 3);
                removed = true;
                break;
            }
        }
        if (!removed) {
            w.noteData.push_back(pitch);
            w.noteData.push_back(start);
            w.noteData.push_back(stepBeats);
        }
        w.dirtyCallback = true;
        w.gestureEnded = true;  // one history commit per note edit
    }

    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(ImGuiCol_FrameBg));
    ImU32 gridCol = ImGui::GetColorU32(ImGuiCol_TableBorderLight);
    ImU32 beatCol = ImGui::GetColorU32(ImGuiCol_Border);
    ImU32 noteCol = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    // Octave shading for orientation (C rows).
    for (int r = 0; r < rows; ++r) {
        int pitch = w.rollLowPitch + (rows - 1 - r);
        if (pitch % 12 == 0) {
            dl->AddRectFilled(ImVec2(origin.x, origin.y + r * chh),
                              ImVec2(origin.x + width, origin.y + (r + 1) * chh),
                              ImGui::GetColorU32(ImGuiCol_TableRowBgAlt));
        }
    }
    for (int c = 0; c <= cols; ++c) {
        dl->AddLine(ImVec2(origin.x + c * cw, origin.y),
                    ImVec2(origin.x + c * cw, origin.y + height),
                    (c % 4 == 0) ? beatCol : gridCol);
    }
    for (int r = 0; r <= rows; ++r)
        dl->AddLine(ImVec2(origin.x, origin.y + r * chh),
                    ImVec2(origin.x + width, origin.y + r * chh), gridCol);
    for (size_t i = 0; i + 2 < w.noteData.size(); i += 3) {
        int r = rows - 1 - ((int)w.noteData[i] - w.rollLowPitch);
        if (r < 0 || r >= rows) continue;
        float x0 = origin.x + w.noteData[i + 1] / stepBeats * cw;
        float x1 = x0 + w.noteData[i + 2] / stepBeats * cw;
        dl->AddRectFilled(ImVec2(x0 + 1, origin.y + r * chh + 1),
                          ImVec2(x1 - 1, origin.y + (r + 1) * chh - 1),
                          noteCol);
    }
    dl->AddRect(origin, ImVec2(origin.x + width, origin.y + height), beatCol);
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawLabel(UIWidget& w) {
    ImGui::TextUnformatted(w.labelText.empty() ? w.name.c_str()
                                               : w.labelText.c_str());
}

bool drawUIWidget(bridge::UIWidget& w) {
    ImGui::PushID(static_cast<int>(w.id));
    bool tap = false;
    switch (w.kind) {
        case UIWidgetKind::Slider:   drawSlider(w); break;
        case UIWidgetKind::Number:   drawNumber(w); break;
        case UIWidgetKind::Button:   drawButton(w); break;
        case UIWidgetKind::Toggle:   drawToggle(w); break;
        case UIWidgetKind::XY:       drawXY(w); break;
        case UIWidgetKind::Meter:    drawMeter(w); tap = true; break;
        case UIWidgetKind::Scope:    drawScope(w); tap = true; break;
        case UIWidgetKind::Plot:     drawPlot(w); break;
        case UIWidgetKind::Waveform: drawWaveform(w); break;
        case UIWidgetKind::MultiSlider: drawMultiSlider(w); break;
        case UIWidgetKind::Matrix:      drawMatrix(w); break;
        case UIWidgetKind::PianoRoll:   drawPianoRoll(w); break;
        case UIWidgetKind::Label:       drawLabel(w); break;
    }
    ImGui::PopID();
    return tap;
}

bool drawPanelWidgets(bridge::UIState& ui, std::string const& panel) {
    bool anyTaps = false;
    for (auto& wp : ui.widgets) {
        if (wp->panel != panel) continue;
        anyTaps |= drawUIWidget(*wp);
    }
    return anyTaps;
}
