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
//  tzpl_ui_ffi.cpp
//  bridge
//
//  FFI bridge for the `ui` lang module. All functions here execute on
//  whatever thread runs the VM (REPL eval worker, scheduler) with
//  nrtvm.mtx held. They take UIState::mtx briefly for registry access
//  and never call into the VM while holding it (see tzpl_ui_state.hpp
//  for the lock-order rules).
//

#include "tzpl_ui_ffi.hpp"
#include "tzpl_ui_state.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl.hpp"
#include "value.hpp"
#include "nrt_vm.hpp"
#include "tracing_gc.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_tap.hpp"
#include "tzpl_audio_file.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace bridge {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static AppContext* getAppContext(ts::VM& vm) {
    return static_cast<AppContext*>(vm.userData());
}

static UIState* getUIState(ts::VM& vm) {
    auto* ctx = getAppContext(vm);
    return ctx ? ctx->uiState : nullptr;
}

static const char* regString(ts::VM& vm, u16 reg) {
    return ts::stringData(vm.reg(reg).o);
}

static UISpec specFromRegs(ts::VM& vm, u16 base) {
    // lo, hi, init, warp ordinal, warp param
    UISpec spec;
    spec.lo = vm.reg(base).f;
    spec.hi = vm.reg(base + 1).f;
    spec.init = vm.reg(base + 2).f;
    int warp = static_cast<int>(vm.reg(base + 3).i);
    if (warp < 0 || warp > static_cast<int>(UIWarp::Cubed)) warp = 0;
    spec.warp = static_cast<UIWarp>(warp);
    spec.warpParam = vm.reg(base + 4).f;
    return spec;
}

// Convert an ABI control spec (from a loaded synthdef) to a UISpec.
// ABI warp ordinal = lang ordinal + 1; 0 (None) means unspecified -> linear.
static UISpec specFromControl(tzpl_ControlSpec const& cs) {
    UISpec spec;
    spec.lo = cs.lo;
    spec.hi = cs.hi;
    spec.init = cs.init;
    int warp = static_cast<int>(cs.warp) - 1;
    if (warp < 0 || warp > static_cast<int>(UIWarp::Cubed)) warp = 0;
    spec.warp = static_cast<UIWarp>(warp);
    spec.warpParam = cs.param;
    return spec;
}

// Upsert a widget in the current panel; returns its id.
static i64 upsertWidget(ts::VM& vm, const char* name, UIWidgetKind kind,
                        UISpec const& spec, UISpec const& spec2 = {}) {
    UIState* ui = getUIState(vm);
    if (!ui) return 0;
    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w = ui->upsert(ui->currentPanel, name, kind, spec, spec2);
    return static_cast<i64>(w->id);
}

// Look up the ControlDesc list for the def of a live-engine node.
// Returns false if the node or def is unknown.
static bool controlsForNode(AppContext* ctx, i64 nodeID,
                            std::vector<engine::ControlDesc>& out) {
    if (!ctx || !ctx->engine) return false;
    std::string defName;
    {
        std::lock_guard<std::mutex> lock(ctx->nodeDefNamesMtx);
        auto it = ctx->nodeDefNames.find(nodeID);
        if (it == ctx->nodeDefNames.end()) return false;
        defName = it->second;
    }
    return engine::listDefControls(ctx->engine, defName.c_str(), out);
}

// ---------------------------------------------------------------------------
// Widget constructors
// ---------------------------------------------------------------------------

// fn uiPanel(name String) Void
static void ffi_uiPanel(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    const char* name = regString(vm, argBase);
    std::lock_guard<std::mutex> lock(ui->mtx);
    ui->currentPanel = name;
}

// fn uiGetPanel() String
static void ffi_uiGetPanel(ts::VM& vm, u16 dst, u16, u16) {
    UIState* ui = getUIState(vm);
    std::string panel;
    if (ui) {
        std::lock_guard<std::mutex> lock(ui->mtx);
        panel = ui->currentPanel;
    }
    // Allocate after releasing ui->mtx (allocation may run the GC).
    vm.reg(dst).o = new ts::StringObj(panel);
}

// fn uiNodeDefName(node Int) String
// The def name of a live-engine node ("" if unknown). Used by ui.x to
// default a node's derived widgets into a panel named after its synthdef.
static void ffi_uiNodeDefName(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    i64 nodeID = vm.reg(argBase).i;
    std::string name;
    if (ctx) {
        std::lock_guard<std::mutex> lock(ctx->nodeDefNamesMtx);
        auto it = ctx->nodeDefNames.find(nodeID);
        if (it != ctx->nodeDefNames.end()) name = it->second;
    }
    vm.reg(dst).o = new ts::StringObj(name);
}

// fn uiSlider(name String, lo Float, hi Float, init Float,
//             warp Int, warpParam Float) Int
static void ffi_uiSlider(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* name = regString(vm, argBase);
    UISpec spec = specFromRegs(vm, argBase + 1);
    vm.reg(dst).i = upsertWidget(vm, name, UIWidgetKind::Slider, spec);
}

// fn uiNumber(name String, init Float) Int
static void ffi_uiNumber(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* name = regString(vm, argBase);
    UISpec spec;
    spec.lo = -1.0e18;
    spec.hi = 1.0e18;
    spec.init = vm.reg(argBase + 1).f;
    vm.reg(dst).i = upsertWidget(vm, name, UIWidgetKind::Number, spec);
}

// fn uiButton(name String) Int
static void ffi_uiButton(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* name = regString(vm, argBase);
    UISpec spec;  // 0..1, init 0
    vm.reg(dst).i = upsertWidget(vm, name, UIWidgetKind::Button, spec);
}

// fn uiToggle(name String, init Bool) Int
static void ffi_uiToggle(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* name = regString(vm, argBase);
    UISpec spec;
    spec.init = vm.reg(argBase + 1).i ? 1.0 : 0.0;
    vm.reg(dst).i = upsertWidget(vm, name, UIWidgetKind::Toggle, spec);
}

// fn uiXY(name String, xlo,xhi,xinit Float, xwarp Int, xparam Float,
//                      ylo,yhi,yinit Float, ywarp Int, yparam Float) Int
static void ffi_uiXY(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* name = regString(vm, argBase);
    UISpec sx = specFromRegs(vm, argBase + 1);
    UISpec sy = specFromRegs(vm, argBase + 6);
    vm.reg(dst).i = upsertWidget(vm, name, UIWidgetKind::XY, sx, sy);
}

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

// Shared body for uiOnChange / uiOnChangeXY: store the closure on the widget.
// The UIState root scanner keeps it alive across GC cycles.
static void storeOnChange(ts::VM& vm, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    ts::Obj* handler = vm.reg(argBase + 1).o;
    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id)) {
        w->onChange = handler;
    } else {
        std::fprintf(stderr, "ui.onChange: no widget with id %llu\n",
                     static_cast<unsigned long long>(id));
    }
}

// fn uiOnChange(id Int, f fn(Float) Void) Void
static void ffi_uiOnChange(ts::VM& vm, u16, u16, u16 argBase) {
    storeOnChange(vm, argBase);
}

// fn uiOnChangeXY(id Int, f fn(Float, Float) Void) Void
static void ffi_uiOnChangeXY(ts::VM& vm, u16, u16, u16 argBase) {
    storeOnChange(vm, argBase);
}

// Resolve a control name on a node to an engine target. Returns tzpl_SErr.
static tzpl_SErr resolveTarget(ts::VM& vm, i64 nodeID, const char* controlName,
                               int silo, UIEngineTarget& out) {
    auto* ctx = getAppContext(vm);
    std::vector<engine::ControlDesc> controls;
    if (!controlsForNode(ctx, nodeID, controls)) return tzpl_errNodeNotFound;
    for (auto const& c : controls) {
        if (c.name == controlName) {
            out.nodeID = static_cast<long>(nodeID);
            out.controlID = static_cast<long>(c.controlID);
            out.silo = silo;
            return tzpl_errNone;
        }
    }
    return tzpl_errControlNotFound;
}

// fn uiBindControl(id Int, node Int, control String, silo Int) Int
static void ffi_uiBindControl(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    i64 nodeID = vm.reg(argBase + 1).i;
    const char* control = regString(vm, argBase + 2);
    int silo = static_cast<int>(vm.reg(argBase + 3).i);

    UIEngineTarget target;
    tzpl_SErr err = ui ? resolveTarget(vm, nodeID, control, silo, target)
                       : tzpl_errInternal;
    if (err == tzpl_errNone) {
        std::lock_guard<std::mutex> lock(ui->mtx);
        if (UIWidget* w = ui->findById(id)) {
            w->target = target;
            // Push the widget's current value through the new binding.
            w->dirtyEngine = true;
        } else {
            err = tzpl_errInternal;
        }
    }
    if (err != tzpl_errNone) {
        std::fprintf(stderr, "ui.bindControl: node %lld control \"%s\": error %d\n",
                     static_cast<long long>(nodeID), control, (int)err);
    }
    vm.reg(dst).i = static_cast<i64>(err);
}

// fn uiBindControlY(id Int, node Int, control String, silo Int) Int
// Binds the Y axis of an XY widget.
static void ffi_uiBindControlY(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    i64 nodeID = vm.reg(argBase + 1).i;
    const char* control = regString(vm, argBase + 2);
    int silo = static_cast<int>(vm.reg(argBase + 3).i);

    UIEngineTarget target;
    tzpl_SErr err = ui ? resolveTarget(vm, nodeID, control, silo, target)
                       : tzpl_errInternal;
    if (err == tzpl_errNone) {
        std::lock_guard<std::mutex> lock(ui->mtx);
        if (UIWidget* w = ui->findById(id)) {
            w->target2 = target;
            w->dirtyEngine = true;
        } else {
            err = tzpl_errInternal;
        }
    }
    if (err != tzpl_errNone) {
        std::fprintf(stderr, "ui.bindControlY: node %lld control \"%s\": error %d\n",
                     static_cast<long long>(nodeID), control, (int)err);
    }
    vm.reg(dst).i = static_cast<i64>(err);
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

// fn uiValue(id Int) Float
static void ffi_uiValue(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    double v = 0.0;
    if (ui) {
        std::lock_guard<std::mutex> lock(ui->mtx);
        if (UIWidget* w = ui->findById(id); w && !w->values.empty())
            v = w->values[0];
    }
    vm.reg(dst).f = v;
}

// fn uiValueY(id Int) Float
static void ffi_uiValueY(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    double v = 0.0;
    if (ui) {
        std::lock_guard<std::mutex> lock(ui->mtx);
        if (UIWidget* w = ui->findById(id); w && w->values.size() > 1)
            v = w->values[1];
    }
    vm.reg(dst).f = v;
}

// fn uiSetValue(id Int, v Float) Void
// Moves the widget and fires its bindings (coalesced, next GUI frame).
static void ffi_uiSetValue(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    double v = vm.reg(argBase + 1).f;
    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id); w && !w->values.empty()) {
        w->values[0] = w->spec.clamp(v);
        w->dirtyEngine = true;
        w->dirtyCallback = true;
    }
}

// fn uiSetValueXY(id Int, x Float, y Float) Void
static void ffi_uiSetValueXY(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    double x = vm.reg(argBase + 1).f;
    double y = vm.reg(argBase + 2).f;
    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id); w && w->values.size() > 1) {
        w->values[0] = w->spec.clamp(x);
        w->values[1] = w->spec2.clamp(y);
        w->dirtyEngine = true;
        w->dirtyCallback = true;
    }
}

// Remove an engine tap (best-effort; logs on failure). Must be called
// WITHOUT ui->mtx held: bundle submission takes the engine's NRT lock.
static void untapWidget(AppContext* ctx, long tapID, int silo) {
    if (!ctx || !ctx->engine || tapID == 0) return;
    tzpl_SErr err = engine::begin(ctx->engine);
    if (err == tzpl_errNone) {
        engine::untap(tapID);
        err = engine::go(silo);
    }
    if (err != tzpl_errNone) {
        std::fprintf(stderr, "ui: untap %ld failed (%d)\n", tapID, (int)err);
    }
}

// fn uiRemove(id Int) Void
static void ffi_uiRemove(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    long tapID = 0;
    int tapSilo = 0;
    {
        std::lock_guard<std::mutex> lock(ui->mtx);
        if (UIWidget* w = ui->findById(id)) {
            tapID = w->tapID;
            tapSilo = w->tapSilo;
        }
        ui->remove(id);
    }
    untapWidget(getAppContext(vm), tapID, tapSilo);
}

// fn uiClear() Void
static void ffi_uiClear(ts::VM& vm, u16, u16, u16) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    std::vector<std::pair<long, int>> taps;
    {
        std::lock_guard<std::mutex> lock(ui->mtx);
        for (auto& w : ui->widgets) {
            if (w->tapID) taps.push_back({w->tapID, w->tapSilo});
        }
        ui->clear();
    }
    for (auto [tapID, silo] : taps) {
        untapWidget(getAppContext(vm), tapID, silo);
    }
}

// ---------------------------------------------------------------------------
// Tap widgets (meters/scopes)
// ---------------------------------------------------------------------------

// Shared body for uiMeter / uiScope: upsert the widget, allocate a tap id,
// and install the engine tap in its own bundle. Engine calls happen outside
// ui->mtx (bundle submit takes the engine's NRT lock).
static i64 makeTapWidget(ts::VM& vm, const char* name, UIWidgetKind kind,
                         i64 nodeID, int outlet, int silo) {
    UIState* ui = getUIState(vm);
    auto* ctx = getAppContext(vm);
    if (!ui || !ctx || !ctx->engine) return 0;

    long oldTap = 0;
    int oldSilo = 0;
    long tapID = 0;
    i64 widgetID = 0;
    {
        std::lock_guard<std::mutex> lock(ui->mtx);
        UIWidget* w = ui->upsert(ui->currentPanel, name, kind, UISpec{}, UISpec{});
        oldTap = w->tapID;
        oldSilo = w->tapSilo;
        tapID = static_cast<long>(ui->nextTapId++);
        w->tapID = tapID;
        w->tapSilo = silo;
        widgetID = static_cast<i64>(w->id);
    }
    if (oldTap) untapWidget(ctx, oldTap, oldSilo);

    int mode = (kind == UIWidgetKind::Scope) ? engine::tapScope : engine::tapMeter;
    tzpl_SErr err = engine::begin(ctx->engine);
    if (err == tzpl_errNone) {
        engine::tapOutlet(nodeID, outlet, tapID, mode);
        err = engine::go(silo);
    }
    if (err != tzpl_errNone) {
        std::fprintf(stderr,
                     "ui: tap on node %lld outlet %d failed (%d)\n",
                     static_cast<long long>(nodeID), outlet, (int)err);
        std::lock_guard<std::mutex> lock(ui->mtx);
        if (UIWidget* w = ui->findById(static_cast<std::uint64_t>(widgetID)))
            w->tapID = 0;
    }
    return widgetID;
}

// ---------------------------------------------------------------------------
// Tap reads from lang (scripts reacting to levels, tests). These read the
// engine tap directly, bypassing the GUI's per-frame value mirror. Note the
// scope FIFO has ONE consumer stream: samples drained here don't reach the
// GUI scope display and vice versa.
// ---------------------------------------------------------------------------

static long widgetTapID(ts::VM& vm, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return 0;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w = ui->findById(id);
    return w ? w->tapID : 0;
}

// fn uiTapPeak(id Int) Float
static void ffi_uiTapPeak(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    long tapID = widgetTapID(vm, argBase);
    vm.reg(dst).f = (ctx && ctx->engine && tapID)
                  ? engine::tapPeak(ctx->engine, tapID) : 0.0;
}

// fn uiTapRms(id Int) Float
static void ffi_uiTapRms(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    long tapID = widgetTapID(vm, argBase);
    vm.reg(dst).f = (ctx && ctx->engine && tapID)
                  ? engine::tapRms(ctx->engine, tapID) : 0.0;
}

// fn uiTapSamples(id Int, max Int) Array[Float]
static void ffi_uiTapSamples(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    long tapID = widgetTapID(vm, argBase);
    int maxSamples = static_cast<int>(vm.reg(argBase + 1).i);
    maxSamples = std::min(std::max(maxSamples, 0), 65536);

    std::vector<float> buf(maxSamples);
    int n = 0;
    if (ctx && ctx->engine && tapID && maxSamples > 0) {
        n = engine::tapDrain(ctx->engine, tapID, buf.data(), maxSamples);
    }
    auto* arrType = vm.arrayType(vm.floatType());
    auto* arr = new ts::PodArray<f64>(arrType);
    arr->v.reserve(n);
    for (int i = 0; i < n; ++i) arr->v.push_back(buf[i]);
    vm.reg(dst).o = arr;
}

// ---------------------------------------------------------------------------
// Plot / waveform widgets
// ---------------------------------------------------------------------------

static void readFloatArray(ts::VM& vm, u16 reg, std::vector<float>& out) {
    auto* arr = vm.reg(reg).o;
    auto n = ts::arraySize(arr);
    out.resize(n);
    for (usize i = 0; i < n; ++i) {
        out[i] = static_cast<float>(ts::arrayGetFloat(arr, i));
    }
}

// fn uiPlot(name String, data Array[Float]) Int
static void ffi_uiPlot(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) { vm.reg(dst).i = 0; return; }
    const char* name = regString(vm, argBase);
    std::vector<float> data;
    readFloatArray(vm, argBase + 1, data);

    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w = ui->upsert(ui->currentPanel, name, UIWidgetKind::Plot,
                             UISpec{}, UISpec{});
    w->plotData = std::move(data);
    vm.reg(dst).i = static_cast<i64>(w->id);
}

// fn uiSetData(id Int, data Array[Float]) Void
static void ffi_uiSetData(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    std::vector<float> data;
    readFloatArray(vm, argBase + 1, data);

    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id)) {
        w->plotData = std::move(data);
    }
}

// fn uiWaveform(name String, node Int, buf Int) Int
// Displays the audio file loaded into a node's buffer slot. Re-reads the
// file recorded by audio_engine.loadBuffer and builds a min/max overview
// (no engine readback involved).
static void ffi_uiWaveform(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    auto* ctx = getAppContext(vm);
    vm.reg(dst).i = 0;
    if (!ui || !ctx) return;
    const char* name = regString(vm, argBase);
    i64 nodeID = vm.reg(argBase + 1).i;
    i64 bufID = vm.reg(argBase + 2).i;

    std::string path;
    {
        std::lock_guard<std::mutex> lock(ctx->bufferPathsMtx);
        auto it = ctx->bufferPaths.find({nodeID, bufID});
        if (it == ctx->bufferPaths.end()) {
            std::fprintf(stderr,
                         "ui.waveform: no file loaded for node %lld buf %lld\n",
                         static_cast<long long>(nodeID),
                         static_cast<long long>(bufID));
            return;
        }
        path = it->second;
    }

    tzpl_Buffer* buffer = tzpl_loadAudioFile(path.c_str(), 0, 0, INT64_MAX);
    if (!buffer) {
        std::fprintf(stderr, "ui.waveform: cannot read \"%s\"\n", path.c_str());
        return;
    }

    // Min/max overview of channel 0.
    constexpr int kBins = 2048;
    std::int64_t frames = buffer->length;
    int bins = static_cast<int>(std::min<std::int64_t>(kBins, frames));
    std::vector<float> mins(bins, 0.0f), maxs(bins, 0.0f);
    double const* ch0 = buffer->data[0];
    for (int b = 0; b < bins; ++b) {
        std::int64_t i0 = frames * b / bins;
        std::int64_t i1 = frames * (b + 1) / bins;
        if (i1 <= i0) i1 = i0 + 1;
        float lo = static_cast<float>(ch0[i0]), hi = lo;
        for (std::int64_t i = i0 + 1; i < i1 && i < frames; ++i) {
            float v = static_cast<float>(ch0[i]);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        mins[b] = lo;
        maxs[b] = hi;
    }
    tzpl_freeBuffer(buffer);

    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w = ui->upsert(ui->currentPanel, name, UIWidgetKind::Waveform,
                             UISpec{}, UISpec{});
    w->waveMin = std::move(mins);
    w->waveMax = std::move(maxs);
    w->waveFrames = frames;
    vm.reg(dst).i = static_cast<i64>(w->id);
}

// fn uiMeter(name String, node Int, outlet Int, silo Int) Int
static void ffi_uiMeter(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* name = regString(vm, argBase);
    i64 node = vm.reg(argBase + 1).i;
    int outlet = static_cast<int>(vm.reg(argBase + 2).i);
    int silo = static_cast<int>(vm.reg(argBase + 3).i);
    vm.reg(dst).i = makeTapWidget(vm, name, UIWidgetKind::Meter, node, outlet, silo);
}

// fn uiScope(name String, node Int, outlet Int, silo Int) Int
static void ffi_uiScope(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* name = regString(vm, argBase);
    i64 node = vm.reg(argBase + 1).i;
    int outlet = static_cast<int>(vm.reg(argBase + 2).i);
    int silo = static_cast<int>(vm.reg(argBase + 3).i);
    vm.reg(dst).i = makeTapWidget(vm, name, UIWidgetKind::Scope, node, outlet, silo);
}

// ---------------------------------------------------------------------------
// Synthdef-derived widgets (materialize a node's interface)
// ---------------------------------------------------------------------------

// Create a widget for one ControlDesc of `nodeID` and bind it fast-path.
// Requires ui->mtx NOT held. Returns the widget id (0 on failure).
static i64 widgetForControl(ts::VM& vm, i64 nodeID, int silo,
                            engine::ControlDesc const& c) {
    UIState* ui = getUIState(vm);
    if (!ui) return 0;

    UISpec spec = specFromControl(c.spec);
    UIWidgetKind kind = UIWidgetKind::Slider;
    switch (c.spec.kind) {
        case tzpl_ckContinuous: kind = UIWidgetKind::Slider; break;
        case tzpl_ckTrigger:    kind = UIWidgetKind::Button; break;
        case tzpl_ckBoolean:    kind = UIWidgetKind::Toggle; break;
        case tzpl_ckSelect:     kind = UIWidgetKind::Number; break;
    }

    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w = ui->upsert(ui->currentPanel, c.name, kind, spec, {});
    w->target = UIEngineTarget{static_cast<long>(nodeID),
                               static_cast<long>(c.controlID), silo};
    w->dirtyEngine = true;  // push current value through the fresh binding
    return static_cast<i64>(w->id);
}

// fn uiControl(node Int, control String, silo Int) Int
// Widget named, specced, and bound entirely from the def's ControlDef.
// Returns the widget id, or 0 if the node/control is unknown.
static void ffi_uiControl(ts::VM& vm, u16 dst, u16, u16 argBase) {
    i64 nodeID = vm.reg(argBase).i;
    const char* control = regString(vm, argBase + 1);
    int silo = static_cast<int>(vm.reg(argBase + 2).i);

    std::vector<engine::ControlDesc> controls;
    i64 id = 0;
    if (controlsForNode(getAppContext(vm), nodeID, controls)) {
        for (auto const& c : controls) {
            if (c.name == control) {
                id = widgetForControl(vm, nodeID, silo, c);
                break;
            }
        }
    }
    if (id == 0) {
        std::fprintf(stderr, "ui.control: node %lld has no control \"%s\"\n",
                     static_cast<long long>(nodeID), control);
    }
    vm.reg(dst).i = id;
}

// fn uiControls(node Int, silo Int) Array[Int]
// Materialize the node's whole interface; returns the widget ids.
static void ffi_uiControls(ts::VM& vm, u16 dst, u16, u16 argBase) {
    i64 nodeID = vm.reg(argBase).i;
    int silo = static_cast<int>(vm.reg(argBase + 1).i);

    std::vector<engine::ControlDesc> controls;
    std::vector<i64> ids;
    if (controlsForNode(getAppContext(vm), nodeID, controls)) {
        for (auto const& c : controls) {
            if (i64 id = widgetForControl(vm, nodeID, silo, c))
                ids.push_back(id);
        }
    } else {
        std::fprintf(stderr, "ui.controls: unknown node %lld\n",
                     static_cast<long long>(nodeID));
    }

    // Build the result array after all registry work (allocation may GC).
    auto* arrType = vm.arrayType(vm.intType());
    auto* arr = new ts::PodArray<i64>(arrType);
    arr->v.assign(ids.begin(), ids.end());
    vm.reg(dst).o = arr;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerUIFFI(ts::Compiler& compiler) {
    auto* Void   = compiler.voidType();
    auto* Int    = compiler.intType();
    auto* Float  = compiler.floatType();
    auto* Bool   = compiler.boolType();
    auto* String = compiler.stringType();
    ts::Type* IntArray = reinterpret_cast<ts::Type*>(compiler.arrayType(Int));
    ts::Type* FloatArray = reinterpret_cast<ts::Type*>(compiler.arrayType(Float));

    // fn(Float) Void and fn(Float, Float) Void for onChange handlers.
    ts::Vec<ts::Type*> fArgs1; fArgs1.push_back(Float);
    ts::Type* FnFloat = reinterpret_cast<ts::Type*>(compiler.functionType(fArgs1, Void));
    ts::Vec<ts::Type*> fArgs2; fArgs2.push_back(Float); fArgs2.push_back(Float);
    ts::Type* FnFloat2 = reinterpret_cast<ts::Type*>(compiler.functionType(fArgs2, Void));

    using R = void (*)(ts::VM&, u16, u16, u16);
    auto reg = [&](const char* name, ts::Type* retType,
                   std::vector<ts::Type*> params, R fn) {
        compiler.registerForeignModuleFunction("ui_ffi", name, retType,
                                               std::move(params), fn,
                                               /*pure=*/false, /*rtSafe=*/false);
    };

    reg("uiPanel",        Void, {String},                       ffi_uiPanel);
    reg("uiGetPanel",     String, {},                           ffi_uiGetPanel);
    reg("uiNodeDefName",  String, {Int},                        ffi_uiNodeDefName);
    reg("uiSlider",       Int,  {String, Float, Float, Float, Int, Float}, ffi_uiSlider);
    reg("uiNumber",       Int,  {String, Float},                ffi_uiNumber);
    reg("uiButton",       Int,  {String},                       ffi_uiButton);
    reg("uiToggle",       Int,  {String, Bool},                 ffi_uiToggle);
    reg("uiXY",           Int,  {String, Float, Float, Float, Int, Float,
                                         Float, Float, Float, Int, Float}, ffi_uiXY);
    reg("uiOnChange",     Void, {Int, FnFloat},                 ffi_uiOnChange);
    reg("uiOnChangeXY",   Void, {Int, FnFloat2},                ffi_uiOnChangeXY);
    reg("uiBindControl",  Int,  {Int, Int, String, Int},        ffi_uiBindControl);
    reg("uiBindControlY", Int,  {Int, Int, String, Int},        ffi_uiBindControlY);
    reg("uiValue",        Float, {Int},                         ffi_uiValue);
    reg("uiValueY",       Float, {Int},                         ffi_uiValueY);
    reg("uiSetValue",     Void, {Int, Float},                   ffi_uiSetValue);
    reg("uiSetValueXY",   Void, {Int, Float, Float},            ffi_uiSetValueXY);
    reg("uiRemove",       Void, {Int},                          ffi_uiRemove);
    reg("uiClear",        Void, {},                             ffi_uiClear);
    reg("uiMeter",        Int,  {String, Int, Int, Int},        ffi_uiMeter);
    reg("uiScope",        Int,  {String, Int, Int, Int},        ffi_uiScope);
    reg("uiPlot",         Int,  {String, FloatArray},           ffi_uiPlot);
    reg("uiSetData",      Void, {Int, FloatArray},              ffi_uiSetData);
    reg("uiWaveform",     Int,  {String, Int, Int},             ffi_uiWaveform);
    reg("uiTapPeak",      Float, {Int},                         ffi_uiTapPeak);
    reg("uiTapRms",       Float, {Int},                         ffi_uiTapRms);
    reg("uiTapSamples",   FloatArray, {Int, Int},               ffi_uiTapSamples);
    reg("uiControl",      Int,  {Int, String, Int},             ffi_uiControl);
    reg("uiControls",     IntArray, {Int, Int},                 ffi_uiControls);
}

void registerUIRootScanner(ts::NRTVM& nrtvm, UIState& ui) {
    nrtvm.vm.addExtraRootScanner([&ui](ts::TracingGC& gc) {
        std::lock_guard<std::mutex> lock(ui.mtx);
        for (auto& w : ui.widgets) {
            if (w->onChange) gc.mark(static_cast<ts::GCObj*>(w->onChange));
        }
    });
}

} // namespace bridge
