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

//
//  controls_dispatch.cpp
//  app (JUCE)
//

#include "controls_dispatch.hpp"
#include "tzpl_ui_state.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_client_interface.hpp"
#include "nrt_vm.hpp"
#include "value.hpp"
#include <algorithm>
#include <cstdio>

namespace tzplapp {

using bridge::UIWidget;
using bridge::UIWidgetKind;

namespace {

struct EngineSend {
    long nodeID;
    long controlID;
    int silo;
    std::vector<float> values;
};

struct CallbackCall {
    ts::Obj* fn;
    double v0, v1;
    int argc;                 // 1 or 2 scalar args, or -1 = one array arg
    std::vector<double> vec;
};

// ~30 Hz; keep running for a short tail after the last work so a burst of
// gestures/prints settles before the timer stops.
constexpr int kHz = 30;
constexpr int kIdleTailTicks = 15;

}

ControlsDispatcher::ControlsDispatcher(bridge::AppContext& appCtx)
    : appCtx_(appCtx) {}

void ControlsDispatcher::ensureRunning() {
    idleTicks_ = 0;
    if (!isTimerRunning()) startTimerHz(kHz);
}

void ControlsDispatcher::queuePanelRemoval(std::vector<std::string> const& panels) {
    pendingClosedPanels_.insert(pendingClosedPanels_.end(),
                                panels.begin(), panels.end());
    ensureRunning();
}

bool ControlsDispatcher::hasWork() {
    auto* ui = appCtx_.uiState;
    if (!ui) return false;
    if (!pendingClosedPanels_.empty()) return true;
    std::lock_guard<std::mutex> lock(ui->mtx);
    for (auto& wp : ui->widgets) {
        if (wp->dirtyEngine || wp->dirtyCallback || wp->gestureActive
            || wp->gestureEnded || wp->tapID != 0)
            return true;
    }
    return false;
}

void ControlsDispatcher::tick() {
    auto* ui = appCtx_.uiState;
    auto& ctx = appCtx_;
    if (!ui) { stopTimer(); return; }

    // ---- Remove widgets of panels whose window was closed. ---------------
    if (!pendingClosedPanels_.empty()) {
        std::vector<std::pair<long, int>> taps;
        {
            std::lock_guard<std::mutex> lock(ui->mtx);
            for (auto const& panel : pendingClosedPanels_) {
                for (auto& w : ui->widgets)
                    if (bridge::panelUnderRoot(w->panel, panel) && w->tapID)
                        taps.push_back({ w->tapID, w->tapSilo });
                std::erase_if(ui->widgets, [&](auto const& w) {
                    return bridge::panelUnderRoot(w->panel, panel);
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
    bool tapsVisible = false;
    if (ctx.engine) {
        std::lock_guard<std::mutex> lock(ui->mtx);
        for (auto& wp : ui->widgets) {
            UIWidget& w = *wp;
            if (w.tapID == 0) continue;
            tapsVisible = true;
            if (w.kind == UIWidgetKind::Meter) {
                if (w.values.size() < 2) w.values.resize(2, 0.0);
                w.values[0] = engine::tapRms(ctx.engine, w.tapID);
                w.values[1] = engine::tapPeak(ctx.engine, w.tapID);
            } else if (w.kind == UIWidgetKind::Scope) {
                int chans = std::max(1, engine::tapChans(ctx.engine, w.tapID));
                w.scopeChans = chans;
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
    if (tapsVisible && onTapsAdvanced) onTapsAdvanced();
    tapsVisibleLastTick_ = tapsVisible;

    // ---- Engine fast path: collect under ui.mtx, send after releasing. ----
    std::vector<EngineSend> sends;
    {
        std::lock_guard<std::mutex> lock(ui->mtx);
        for (auto& wp : ui->widgets) {
            UIWidget& w = *wp;
            if (!w.dirtyEngine) continue;
            if (w.target) {
                std::vector<float> vals;
                if (w.kind == UIWidgetKind::MultiSlider
                    || w.kind == UIWidgetKind::Matrix) {
                    vals.assign(w.values.begin(), w.values.end());
                } else {
                    vals.push_back(static_cast<float>(w.values[0]));
                }
                sends.push_back({ w.target->nodeID, w.target->controlID,
                                  w.target->silo, std::move(vals) });
            }
            if (w.target2 && w.values.size() > 1) {
                sends.push_back({ w.target2->nodeID, w.target2->controlID,
                                  w.target2->silo,
                                  { static_cast<float>(w.values[1]) } });
            }
            w.dirtyEngine = false;
        }
    }
    if (!sends.empty() && ctx.engine) {
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
                                       (int)sends[i].values.size(),
                                       sends[i].values.data());
                }
            }
            if (err == tzpl_errNone) err = engine::go(silo);
            if (err != tzpl_errNone) {
                std::fprintf(stderr,
                             "ui: control send to silo %d failed (%d)\n",
                             silo, (int)err);
            }
        }
    }

    // ---- Callbacks: deliver latest values if the VM mutex is free. --------
    if (ctx.nrtvm) {
        bool any = false;
        {
            std::lock_guard<std::mutex> lock(ui->mtx);
            for (auto& wp : ui->widgets) {
                if (wp->dirtyCallback && wp->onChange) { any = true; break; }
                if (wp->dirtyCallback && !wp->onChange) wp->dirtyCallback = false;
            }
        }
        if (any) {
            std::unique_lock<std::mutex> vmLock(ctx.nrtvm->mtx, std::try_to_lock);
            if (vmLock.owns_lock()) {
                std::vector<CallbackCall> calls;
                {
                    std::lock_guard<std::mutex> lock(ui->mtx);
                    for (auto& wp : ui->widgets) {
                        UIWidget& w = *wp;
                        if (!w.dirtyCallback || !w.onChange) continue;
                        CallbackCall call{};
                        call.fn = w.onChange;
                        if (w.kind == UIWidgetKind::PianoRoll) {
                            call.argc = -1;
                            call.vec.assign(w.noteData.begin(), w.noteData.end());
                        } else if (w.kind == UIWidgetKind::MultiSlider
                                   || w.kind == UIWidgetKind::Matrix) {
                            call.argc = -1;
                            call.vec = w.values;
                        } else {
                            call.v0 = w.values[0];
                            call.v1 = w.values.size() > 1 ? w.values[1] : 0.0;
                            call.argc = (w.kind == UIWidgetKind::XY
                                         || w.kind == UIWidgetKind::Range) ? 2 : 1;
                        }
                        calls.push_back(std::move(call));
                        w.dirtyCallback = false;
                    }
                }
                if (!calls.empty()) {
                    ctx.nrtvm->vm.makeCurrent();
                    for (auto const& c : calls) {
                        ts::Word args[2];
                        if (c.argc < 0) {
                            auto* arr = new ts::PodArray<f64>(
                                ctx.nrtvm->vm.arrayType(ctx.nrtvm->vm.floatType()));
                            arr->v.assign(c.vec.begin(), c.vec.end());
                            args[0].o = arr;
                            ctx.nrtvm->vm.callCallable(c.fn, args, 1);
                        } else {
                            args[0].f = c.v0;
                            args[1].f = c.v1;
                            ctx.nrtvm->vm.callCallable(c.fn, args,
                                                       static_cast<u16>(c.argc));
                        }
                    }
                    ctx.nrtvm->vm.gcHeartbeat();
                }
            }
        }
    }

    // ---- Demand-driven stop: quit after a short idle tail. ---------------
    if (hasWork()) idleTicks_ = 0;
    else if (++idleTicks_ > kIdleTailTicks) stopTimer();
}

}
