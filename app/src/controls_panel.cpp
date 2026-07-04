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

#include "controls_panel.hpp"

#include "tzpl_ui_state.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_client_interface.hpp"
#include "nrt_vm.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Widget drawing. All widget interaction happens under ui.mtx (brief, GUI
// thread only). Never call into the VM from here (lock order: see
// tzpl_ui_state.hpp).
// ---------------------------------------------------------------------------

using bridge::UIWidget;
using bridge::UIWidgetKind;

static void markDirty(UIWidget& w) {
    w.dirtyEngine = true;
    w.dirtyCallback = true;
}

static void drawSlider(UIWidget& w) {
    float pos = static_cast<float>(w.spec.unmap(w.values[0]));
    char display[64];
    std::snprintf(display, sizeof(display), "%.4g", w.values[0]);
    ImGui::SetNextItemWidth(-140.0f);
    if (ImGui::SliderFloat((std::string("##") + w.name).c_str(), &pos,
                           0.0f, 1.0f, display)) {
        w.values[0] = w.spec.map(pos);
        markDirty(w);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawNumber(UIWidget& w) {
    double v = w.values[0];
    ImGui::SetNextItemWidth(-140.0f);
    if (ImGui::DragScalar((std::string("##") + w.name).c_str(),
                          ImGuiDataType_Double, &v, 0.01f, nullptr, nullptr,
                          "%.4g")) {
        w.values[0] = w.spec.clamp(v);
        markDirty(w);
    }
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
}

static void drawXY(UIWidget& w) {
    const float size = 160.0f;
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

void ControlsPanel::draw(bridge::UIState& ui) {
    std::lock_guard<std::mutex> lock(ui.mtx);
    if (ui.widgets.empty()) return;

    // Group widgets by panel, preserving insertion order of first appearance.
    std::vector<std::string> panels;
    for (auto& w : ui.widgets) {
        if (std::find(panels.begin(), panels.end(), w->panel) == panels.end())
            panels.push_back(w->panel);
    }

    for (auto const& panel : panels) {
        std::string title = panel.empty() ? "Controls" : panel;
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title.c_str())) {
            for (auto& wp : ui.widgets) {
                UIWidget& w = *wp;
                if (w.panel != panel) continue;
                ImGui::PushID(static_cast<int>(w.id));
                switch (w.kind) {
                    case UIWidgetKind::Slider: drawSlider(w); break;
                    case UIWidgetKind::Number: drawNumber(w); break;
                    case UIWidgetKind::Button: drawButton(w); break;
                    case UIWidgetKind::Toggle: drawToggle(w); break;
                    case UIWidgetKind::XY:     drawXY(w);     break;
                }
                ImGui::PopID();
            }
        }
        ImGui::End();
    }
}

// ---------------------------------------------------------------------------
// Event dispatch
// ---------------------------------------------------------------------------

namespace {

struct EngineSend {
    long nodeID;
    long controlID;
    int silo;
    float value;
};

struct CallbackCall {
    ts::Obj* fn;
    double v0, v1;
    int argc;
};

} // namespace

void ControlsPanel::dispatch(bridge::UIState& ui, bridge::AppContext& ctx) {
    // ---- Engine fast path: collect under ui.mtx, send after releasing. ----
    std::vector<EngineSend> sends;
    {
        std::lock_guard<std::mutex> lock(ui.mtx);
        for (auto& wp : ui.widgets) {
            UIWidget& w = *wp;
            if (!w.dirtyEngine) continue;
            if (w.target) {
                sends.push_back({w.target->nodeID, w.target->controlID,
                                 w.target->silo,
                                 static_cast<float>(w.values[0])});
            }
            if (w.target2 && w.values.size() > 1) {
                sends.push_back({w.target2->nodeID, w.target2->controlID,
                                 w.target2->silo,
                                 static_cast<float>(w.values[1])});
            }
            w.dirtyEngine = false;
        }
    }
    if (!sends.empty() && ctx.engine) {
        // One bundle per silo. The bundle is thread-local, so this never
        // collides with bundles built on the eval or scheduler threads.
        std::sort(sends.begin(), sends.end(),
                  [](EngineSend const& a, EngineSend const& b) {
                      return a.silo < b.silo;
                  });
        for (size_t i = 0; i < sends.size();) {
            int silo = sends[i].silo;
            tzpl_SErr err = engine::begin(ctx.engine);
            for (; i < sends.size() && sends[i].silo == silo; ++i) {
                if (err == tzpl_errNone) {
                    engine::setControl(sends[i].nodeID, sends[i].controlID,
                                       1, &sends[i].value);
                }
            }
            if (err == tzpl_errNone) err = engine::go(silo);
            if (err != tzpl_errNone) {
                std::fprintf(stderr, "ui: control send to silo %d failed (%d)\n",
                             silo, (int)err);
            }
        }
    }

    // ---- Callbacks: deliver latest values if the VM mutex is free. --------
    if (!ctx.nrtvm) return;
    {
        // Cheap pre-check to avoid taking the VM mutex every frame.
        std::lock_guard<std::mutex> lock(ui.mtx);
        bool any = false;
        for (auto& wp : ui.widgets) {
            if (wp->dirtyCallback && wp->onChange) { any = true; break; }
            if (wp->dirtyCallback && !wp->onChange) wp->dirtyCallback = false;
        }
        if (!any) return;
    }

    std::unique_lock<std::mutex> vmLock(ctx.nrtvm->mtx, std::try_to_lock);
    if (!vmLock.owns_lock()) return;  // eval in flight; retry next frame

    // Holding nrtvm.mtx: no eval can rebind/remove closures and no GC can
    // run concurrently, so the snapshotted Obj* pointers stay valid.
    std::vector<CallbackCall> calls;
    {
        std::lock_guard<std::mutex> lock(ui.mtx);
        for (auto& wp : ui.widgets) {
            UIWidget& w = *wp;
            if (!w.dirtyCallback || !w.onChange) continue;
            double v1 = w.values.size() > 1 ? w.values[1] : 0.0;
            int argc = (w.kind == UIWidgetKind::XY) ? 2 : 1;
            calls.push_back({w.onChange, w.values[0], v1, argc});
            w.dirtyCallback = false;
        }
    }
    if (calls.empty()) return;

    ctx.nrtvm->vm.makeCurrent();
    for (auto const& c : calls) {
        ts::Word args[2];
        args[0].f = c.v0;
        args[1].f = c.v1;
        ctx.nrtvm->vm.callCallable(c.fn, args, static_cast<u16>(c.argc));
    }
    ctx.nrtvm->vm.gcHeartbeat();
}

bool ControlsPanel::hasPendingEvents(bridge::UIState& ui) {
    std::lock_guard<std::mutex> lock(ui.mtx);
    for (auto& wp : ui.widgets) {
        if (wp->dirtyEngine || wp->dirtyCallback) return true;
    }
    return false;
}
