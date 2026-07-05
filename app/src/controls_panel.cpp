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
#include "widget_draw.hpp"

#include "tzpl_ui_state.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_client_interface.hpp"
#include "nrt_vm.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using bridge::UIWidget;
using bridge::UIWidgetKind;

void ControlsPanel::draw(bridge::UIState& ui,
                         std::vector<std::string> const* skipPanels) {
    std::lock_guard<std::mutex> lock(ui.mtx);
    if (ui.widgets.empty()) return;

    // Group widgets by panel, preserving insertion order of first appearance.
    std::vector<std::string> panels;
    for (auto& w : ui.widgets) {
        if (skipPanels && std::find(skipPanels->begin(), skipPanels->end(),
                                    w->panel) != skipPanels->end())
            continue;
        if (std::find(panels.begin(), panels.end(), w->panel) == panels.end())
            panels.push_back(w->panel);
    }

    for (auto const& panel : panels) {
        std::string title = panel.empty() ? "Controls" : panel;
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
        // Closing a floating panel window REMOVES its widgets (code can
        // recreate them by re-running). Processed in dispatch(), which has
        // the engine context to release taps.
        bool open = true;
        if (ImGui::Begin(title.c_str(), &open)) {
            anyTapsVisible_ |= drawPanelWidgets(ui, panel);
        }
        ImGui::End();
        if (!open) pendingClosedPanels_.push_back(panel);
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
    // ---- Remove widgets of panels whose window was closed. ---------------
    if (!pendingClosedPanels_.empty()) {
        std::vector<std::pair<long, int>> taps;
        {
            std::lock_guard<std::mutex> lock(ui.mtx);
            for (auto const& panel : pendingClosedPanels_) {
                for (auto& w : ui.widgets) {
                    if (w->panel == panel && w->tapID)
                        taps.push_back({w->tapID, w->tapSilo});
                }
                std::erase_if(ui.widgets, [&](auto const& w) {
                    return w->panel == panel;
                });
            }
        }
        pendingClosedPanels_.clear();
        for (auto [tapID, silo] : taps) {
            if (!ctx.engine) break;
            if (engine::begin(ctx.engine) == tzpl_errNone) {
                engine::untap(tapID);
                engine::go(silo);
            }
        }
    }

    // ---- Poll engine taps into meter values / scope rings. ---------------
    // (ui.mtx before the engine's NRT lock inside tapPeak/tapDrain -- the
    // reverse order never occurs: no engine code touches UIState.)
    if (ctx.engine) {
        std::lock_guard<std::mutex> lock(ui.mtx);
        for (auto& wp : ui.widgets) {
            UIWidget& w = *wp;
            if (w.tapID == 0) continue;
            if (w.kind == UIWidgetKind::Meter) {
                w.values[0] = engine::tapRms(ctx.engine, w.tapID);
                w.values[1] = engine::tapPeak(ctx.engine, w.tapID);
            } else if (w.kind == UIWidgetKind::Scope) {
                int chans = std::max(1, engine::tapChans(ctx.engine, w.tapID));
                w.scopeChans = chans;
                // Drain and trim in whole interleaved frames so the ring
                // stays frame-aligned.
                float buf[4096];
                int want = (4096 / chans) * chans;
                int n = engine::tapDrain(ctx.engine, w.tapID, buf, want);
                if (n > 0) {
                    w.scopeRing.insert(w.scopeRing.end(), buf, buf + n);
                    size_t maxRing = (size_t)8192 * chans;
                    if (w.scopeRing.size() > maxRing) {
                        size_t excess = w.scopeRing.size() - maxRing;
                        excess = ((excess + chans - 1) / chans) * chans;
                        w.scopeRing.erase(w.scopeRing.begin(),
                                          w.scopeRing.begin() + excess);
                    }
                }
            }
        }
    }

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
