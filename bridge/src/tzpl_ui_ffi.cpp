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
#include "tzpl_ui_node_controls.hpp"
#include "tzpl_ui_taps.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl.hpp"
#include "value.hpp"
#include "nrt_vm.hpp"
#include "tracing_gc.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_tap.hpp"
#include "tzpl_audio_file.hpp"

#include <cctype>
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

static void readFloatArray(ts::VM& vm, u16 reg, std::vector<float>& out) {
    auto* arr = vm.reg(reg).o;
    auto n = ts::arraySize(arr);
    out.resize(n);
    for (usize i = 0; i < n; ++i) {
        out[i] = static_cast<float>(ts::arrayGetFloat(arr, i));
    }
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

// fn uiRange(name String, lo Float, hi Float, initLo Float, initHi Float,
//            warp Int, warpParam Float) Int
// Two-ended slider: values[0] = lo end, values[1] = hi end, both mapped
// through the same spec.
static void ffi_uiRange(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) { vm.reg(dst).i = 0; return; }
    const char* name = regString(vm, argBase);
    UISpec spec;
    spec.lo = vm.reg(argBase + 1).f;
    spec.hi = vm.reg(argBase + 2).f;
    double initLo = vm.reg(argBase + 3).f;
    double initHi = vm.reg(argBase + 4).f;
    spec.init = initLo;
    int warp = static_cast<int>(vm.reg(argBase + 5).i);
    if (warp < 0 || warp > static_cast<int>(UIWarp::Cubed)) warp = 0;
    spec.warp = static_cast<UIWarp>(warp);
    spec.warpParam = vm.reg(argBase + 6).f;

    std::lock_guard<std::mutex> lock(ui->mtx);
    bool fresh = ui->findByName(ui->currentPanel, name) == nullptr;
    UIWidget* w = ui->upsert(ui->currentPanel, name, UIWidgetKind::Range,
                             spec, UISpec{});
    if (fresh) {
        // An adopted widget keeps its ends (upsert clamps and orders them);
        // a new one starts at the declared initial range.
        w->values[0] = spec.clamp(initLo);
        w->values[1] = spec.clamp(initHi);
        if (w->values[0] > w->values[1]) std::swap(w->values[0], w->values[1]);
    }
    vm.reg(dst).i = static_cast<i64>(w->id);
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

// fn uiMultiSlider(name String, n Int, lo,hi,init Float, warp Int, warpParam Float) Int
static void ffi_uiMultiSlider(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) { vm.reg(dst).i = 0; return; }
    const char* name = regString(vm, argBase);
    int n = std::max(1, (int)vm.reg(argBase + 1).i);
    UISpec spec = specFromRegs(vm, argBase + 2);

    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w = ui->upsert(ui->currentPanel, name, UIWidgetKind::MultiSlider,
                             spec, UISpec{});
    // Resize preserving existing bars; new bars start at init.
    w->values.resize((size_t)n, spec.clamp(spec.init));
    for (auto& v : w->values) v = spec.clamp(v);
    vm.reg(dst).i = (i64)w->id;
}

// fn uiMatrix(name String, rows Int, cols Int) Int
static void ffi_uiMatrix(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) { vm.reg(dst).i = 0; return; }
    const char* name = regString(vm, argBase);
    int rows = std::max(1, (int)vm.reg(argBase + 1).i);
    int cols = std::max(1, (int)vm.reg(argBase + 2).i);
    UISpec spec;  // 0..1

    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w = ui->upsert(ui->currentPanel, name, UIWidgetKind::Matrix,
                             spec, UISpec{});
    if (w->rows != rows || w->cols != cols) {
        // Dimension change: keep overlapping cells, zero the rest.
        std::vector<double> next((size_t)rows * cols, 0.0);
        for (int r = 0; r < std::min(rows, w->rows); ++r)
            for (int c = 0; c < std::min(cols, w->cols); ++c)
                if ((size_t)(r * w->cols + c) < w->values.size())
                    next[(size_t)r * cols + c] = w->values[(size_t)r * w->cols + c];
        w->values = std::move(next);
        w->rows = rows;
        w->cols = cols;
    } else {
        w->values.resize((size_t)rows * cols, 0.0);
    }
    vm.reg(dst).i = (i64)w->id;
}

// fn uiButtonMatrix(name String, rows Int, cols Int, momentary Bool) Int
static void ffi_uiButtonMatrix(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) { vm.reg(dst).i = 0; return; }
    const char* name = regString(vm, argBase);
    int rows = std::max(1, (int)vm.reg(argBase + 1).i);
    int cols = std::max(1, (int)vm.reg(argBase + 2).i);
    bool momentary = vm.reg(argBase + 3).i != 0;
    UISpec spec;  // 0..1

    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w = ui->upsert(ui->currentPanel, name, UIWidgetKind::ButtonMatrix,
                             spec, UISpec{});
    if (w->rows != rows || w->cols != cols) {
        // Dimension change: keep overlapping cells (values AND labels),
        // zero/blank the rest.
        std::vector<double> next((size_t)rows * cols, 0.0);
        std::vector<std::string> nextLabels((size_t)rows * cols);
        for (int r = 0; r < std::min(rows, w->rows); ++r) {
            for (int c = 0; c < std::min(cols, w->cols); ++c) {
                size_t from = (size_t)r * w->cols + c;
                size_t to = (size_t)r * cols + c;
                if (from < w->values.size()) next[to] = w->values[from];
                if (from < w->cellLabels.size())
                    nextLabels[to] = w->cellLabels[from];
            }
        }
        w->values = std::move(next);
        if (!w->cellLabels.empty()) w->cellLabels = std::move(nextLabels);
        w->rows = rows;
        w->cols = cols;
        w->labelsVersion++;
    } else {
        w->values.resize((size_t)rows * cols, 0.0);
    }
    w->momentary = momentary;
    vm.reg(dst).i = (i64)w->id;
}

// fn uiPianoRoll(name String, beats Float, edo Int) Int
static void ffi_uiPianoRoll(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) { vm.reg(dst).i = 0; return; }
    const char* name = regString(vm, argBase);
    double beats = vm.reg(argBase + 1).f;
    int edo = std::max(1, (int)vm.reg(argBase + 2).i);

    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w = ui->upsert(ui->currentPanel, name, UIWidgetKind::PianoRoll,
                             UISpec{}, UISpec{});
    w->rollBeats = (float)std::max(1.0, beats);
    if (w->rollEdo != edo) {
        // Pitch units changed: reset the view window to a sane default
        // for this division (2 octaves starting 4 above the reference).
        // rollRange() afterwards still customizes it.
        w->rollEdo = edo;
        w->rollLowPitch = 4 * edo;
        w->rollRows = 2 * edo;
    }
    vm.reg(dst).i = (i64)w->id;
}

// fn uiLabel(name String, text String) Int
static void ffi_uiLabel(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) { vm.reg(dst).i = 0; return; }
    const char* name = regString(vm, argBase);
    const char* text = regString(vm, argBase + 1);

    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w = ui->upsert(ui->currentPanel, name, UIWidgetKind::Label,
                             UISpec{}, UISpec{});
    w->labelText = text;
    vm.reg(dst).i = (i64)w->id;
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

// fn uiOnChangeVec(id Int, f fn(Array[Float]) Void) Void
// For MultiSlider/Matrix (values) and PianoRoll (note triplets); the
// per-frame dispatch builds the array argument by widget kind.
static void ffi_uiOnChangeVec(ts::VM& vm, u16, u16, u16 argBase) {
    storeOnChange(vm, argBase);
}

// fn uiOnCell(id Int, f fn(Int, Int, Float) Void) Void
// Per-cell event callback for Matrix/ButtonMatrix: fired once per cell
// press/flip/release with (row, col, value), in interaction order --
// unlike uiOnChangeVec, which coalesces to the latest whole array once
// per GUI frame.
static void ffi_uiOnCell(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    ts::Obj* handler = vm.reg(argBase + 1).o;
    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id)) {
        w->onCell = handler;
    } else {
        std::fprintf(stderr, "ui.onCell: no widget with id %llu\n",
                     static_cast<unsigned long long>(id));
    }
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

// fn uiBindKey(id Int, key String) Void
// Key binding: single char 'a'-'z' / '0'-'9' or "space"; "" unbinds.
static void ffi_uiBindKey(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    std::string chord = regString(vm, argBase + 1);
    for (auto& c : chord)
        c = (char)std::tolower((unsigned char)c);
    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id)) w->keyChord = std::move(chord);
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

// fn uiSetRange(id Int, lo Float, hi Float) Void
static void ffi_uiSetRange(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    double lo = vm.reg(argBase + 1).f;
    double hi = vm.reg(argBase + 2).f;
    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id); w && w->values.size() > 1) {
        lo = w->spec.clamp(lo);
        hi = w->spec.clamp(hi);
        if (lo > hi) std::swap(lo, hi);
        w->values[0] = lo;
        w->values[1] = hi;
        w->dirtyEngine = true;
        w->dirtyCallback = true;
    }
}

// Remove an engine tap (best-effort; logs on failure). Must be called
// WITHOUT ui->mtx held: bundle submission takes the engine's NRT lock.
static void untapWidget(AppContext* ctx, long tapID, int silo) {
    if (!ctx) return;
    bridge::untapWidget(ctx->engine, tapID, silo);
}

// fn uiValues(id Int) Array[Float]
static void ffi_uiValues(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    std::vector<double> vals;
    if (ui) {
        std::lock_guard<std::mutex> lock(ui->mtx);
        if (UIWidget* w = ui->findById(id)) vals = w->values;
    }
    auto* arr = new ts::PodArray<f64>(vm.arrayType(vm.floatType()));
    arr->v.assign(vals.begin(), vals.end());
    vm.reg(dst).o = arr;
}

// fn uiSetValues(id Int, vals Array[Float]) Void
static void ffi_uiSetValues(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    std::vector<float> vals;
    readFloatArray(vm, argBase + 1, vals);
    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id)) {
        for (size_t i = 0; i < w->values.size() && i < vals.size(); ++i)
            w->values[i] = w->spec.clamp(vals[i]);
        w->dirtyEngine = true;
        w->dirtyCallback = true;
    }
}

// fn uiCellLabels(id Int) Array[String]
static void ffi_uiCellLabels(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    std::vector<std::string> labels;
    if (ui) {
        std::lock_guard<std::mutex> lock(ui->mtx);
        if (UIWidget* w = ui->findById(id)) labels = w->cellLabels;
    }
    // Allocate after releasing ui->mtx (allocation may run the GC).
    auto* arr = new ts::ObjArray(vm.arrayType(vm.stringType()));
    for (auto const& s : labels) arr->push(new ts::StringObj(s));
    vm.reg(dst).o = arr;
}

// fn uiSetCellLabels(id Int, labels Array[String]) Void
// Row-major; entries beyond rows*cols are dropped, missing ones blank.
static void ffi_uiSetCellLabels(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    // Copy the strings out BEFORE taking ui->mtx (see lock-order rules).
    std::vector<std::string> labels;
    auto* arr = vm.reg(argBase + 1).o;
    auto n = ts::arraySize(arr);
    labels.reserve(n);
    for (usize i = 0; i < n; ++i)
        labels.push_back(ts::stringData(ts::arrayGetObj(arr, i)));
    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id)) {
        size_t cells = (size_t)std::max(1, w->rows) * std::max(1, w->cols);
        if (labels.size() > cells) labels.resize(cells);
        w->cellLabels = std::move(labels);
        w->labelsVersion++;
    }
}

// fn uiSetCellLabel(id Int, row Int, col Int, label String) Void
static void ffi_uiSetCellLabel(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    int row = (int)vm.reg(argBase + 1).i;
    int col = (int)vm.reg(argBase + 2).i;
    std::string label = regString(vm, argBase + 3);
    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id)) {
        int cols = std::max(1, w->cols);
        if (row < 0 || row >= std::max(1, w->rows) || col < 0 || col >= cols)
            return;
        size_t idx = (size_t)row * cols + col;
        if (w->cellLabels.size() <= idx) w->cellLabels.resize(idx + 1);
        w->cellLabels[idx] = std::move(label);
        w->labelsVersion++;
    }
}

// fn uiNotes(id Int) Array[Float]  -- flat (pitch, startBeat, durBeats)
static void ffi_uiNotes(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    std::vector<float> notes;
    if (ui) {
        std::lock_guard<std::mutex> lock(ui->mtx);
        if (UIWidget* w = ui->findById(id)) notes = w->noteData;
    }
    auto* arr = new ts::PodArray<f64>(vm.arrayType(vm.floatType()));
    arr->v.assign(notes.begin(), notes.end());
    vm.reg(dst).o = arr;
}

// fn uiSetNotes(id Int, notes Array[Float]) Void
static void ffi_uiSetNotes(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    std::vector<float> notes;
    readFloatArray(vm, argBase + 1, notes);
    notes.resize(notes.size() - notes.size() % 3);  // whole triplets
    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id)) {
        w->noteData = std::move(notes);
        w->dirtyCallback = true;
    }
}

// fn uiSetFrame(id Int, x,y,w,h Float) Void
static void ffi_uiSetFrame(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id)) {
        w->fx = (float)vm.reg(argBase + 1).f;
        w->fy = (float)vm.reg(argBase + 2).f;
        w->fw = (float)vm.reg(argBase + 3).f;
        w->fh = (float)vm.reg(argBase + 4).f;
    }
}

// fn uiRollRange(id Int, lowPitch Int, rows Int) Void
static void ffi_uiRollRange(ts::VM& vm, u16, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) return;
    auto id = static_cast<std::uint64_t>(vm.reg(argBase).i);
    std::lock_guard<std::mutex> lock(ui->mtx);
    if (UIWidget* w = ui->findById(id)) {
        w->rollLowPitch = (int)vm.reg(argBase + 1).i;
        w->rollRows = std::max(1, (int)vm.reg(argBase + 2).i);
    }
}

// ---------------------------------------------------------------------------
// show(): materialize a value as its natural widget
// ---------------------------------------------------------------------------

// fn uiShowFloat(name String, v Float) Int
static void ffi_uiShowFloat(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) { vm.reg(dst).i = 0; return; }
    const char* name = regString(vm, argBase);
    double v = vm.reg(argBase + 1).f;
    std::lock_guard<std::mutex> lock(ui->mtx);
    UISpec spec;
    spec.lo = -1.0e18; spec.hi = 1.0e18; spec.init = v;
    UIWidget* w = ui->upsert(ui->currentPanel, name, UIWidgetKind::Number,
                             spec, UISpec{});
    w->values[0] = v;
    vm.reg(dst).i = (i64)w->id;
}

// fn uiShowBool(name String, v Bool) Int
static void ffi_uiShowBool(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) { vm.reg(dst).i = 0; return; }
    const char* name = regString(vm, argBase);
    bool v = vm.reg(argBase + 1).i != 0;
    std::lock_guard<std::mutex> lock(ui->mtx);
    UISpec spec;
    spec.init = v ? 1.0 : 0.0;
    UIWidget* w = ui->upsert(ui->currentPanel, name, UIWidgetKind::Toggle,
                             spec, UISpec{});
    w->values[0] = v ? 1.0 : 0.0;
    vm.reg(dst).i = (i64)w->id;
}

// fn uiShowVec(name String, data Array[Float]) Int
// <= 64 elements: an editable multislider ranged around the data;
// larger: a plot.
static void ffi_uiShowVec(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) { vm.reg(dst).i = 0; return; }
    const char* name = regString(vm, argBase);
    std::vector<float> data;
    readFloatArray(vm, argBase + 1, data);

    std::lock_guard<std::mutex> lock(ui->mtx);
    if (data.size() <= 64 && !data.empty()) {
        float lo = 0.0f, hi = 1.0f;
        for (float v : data) { lo = std::min(lo, v); hi = std::max(hi, v); }
        UISpec spec;
        spec.lo = lo; spec.hi = hi; spec.init = 0.0;
        UIWidget* w = ui->upsert(ui->currentPanel, name,
                                 UIWidgetKind::MultiSlider, spec, UISpec{});
        w->values.assign(data.begin(), data.end());
        vm.reg(dst).i = (i64)w->id;
    } else {
        UIWidget* w = ui->upsert(ui->currentPanel, name, UIWidgetKind::Plot,
                                 UISpec{}, UISpec{});
        w->plotData = std::move(data);
        vm.reg(dst).i = (i64)w->id;
    }
}

// fn uiShowString(name String, s String) Int
static void ffi_uiShowString(ts::VM& vm, u16 dst, u16, u16 argBase) {
    UIState* ui = getUIState(vm);
    if (!ui) { vm.reg(dst).i = 0; return; }
    const char* name = regString(vm, argBase);
    const char* text = regString(vm, argBase + 1);
    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w = ui->upsert(ui->currentPanel, name, UIWidgetKind::Label,
                             UISpec{}, UISpec{});
    w->labelText = text;
    vm.reg(dst).i = (i64)w->id;
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

// Shared body for uiMeter / uiScope / uiMasterTap: upsert the widget,
// allocate a tap id, and install the engine tap in its own bundle. Engine
// calls happen outside ui->mtx (bundle submit takes the engine's NRT lock).
//
// nodeID < 0 means the master output bus: the tap goes on silo 0 via
// tapMaster instead of tapOutlet. Everything downstream -- the per-frame
// poll, untap on panel close, document restore, ui.peakLevel -- is unchanged,
// because a master tap is an ordinary entry in the engine's tap registry.
static i64 makeTapWidget(ts::VM& vm, const char* name, UIWidgetKind kind,
                         i64 nodeID, int outlet, int silo) {
    UIState* ui = getUIState(vm);
    auto* ctx = getAppContext(vm);
    if (!ui || !ctx || !ctx->engine) return 0;

    bool master = nodeID < 0;
    if (master) silo = 0; // master taps are silo-0-only

    long oldTap = 0;
    int oldSilo = 0;
    long tapID = static_cast<long>(engine::allocTapID(ctx->engine));
    i64 widgetID = 0;
    {
        std::lock_guard<std::mutex> lock(ui->mtx);
        UIWidget* w = ui->upsert(ui->currentPanel, name, kind, UISpec{}, UISpec{});
        oldTap = w->tapID;
        oldSilo = w->tapSilo;
        w->tapID = tapID;
        w->tapSilo = silo;
        widgetID = static_cast<i64>(w->id);
    }
    if (oldTap) untapWidget(ctx, oldTap, oldSilo);

    int mode = (kind == UIWidgetKind::Meter) ? engine::tapMeter : engine::tapScope;
    tzpl_SErr err = engine::begin(ctx->engine);
    if (err == tzpl_errNone) {
        if (master) engine::tapMaster(tapID, mode);
        else engine::tapOutlet(nodeID, outlet, tapID, mode);
        err = engine::go(silo);
    }
    if (err != tzpl_errNone) {
        if (master) {
            std::fprintf(stderr, "ui: master tap failed (%d)\n", (int)err);
        } else {
            std::fprintf(stderr,
                         "ui: tap on node %lld outlet %d failed (%d)\n",
                         static_cast<long long>(nodeID), outlet, (int)err);
        }
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

// fn uiTapChans(id Int) Int
static void ffi_uiTapChans(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    long tapID = widgetTapID(vm, argBase);
    vm.reg(dst).i = (ctx && ctx->engine && tapID)
                  ? engine::tapChans(ctx->engine, tapID) : 0;
}

// fn uiTapSamples(id Int, max Int) Array[Float]
// Returns interleaved frames of uiTapChans(id) channels. The drain count
// is clamped to a frame multiple so subsequent consumers (this call or
// the GUI scope) stay frame-aligned.
static void ffi_uiTapSamples(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    long tapID = widgetTapID(vm, argBase);
    int maxSamples = static_cast<int>(vm.reg(argBase + 1).i);
    maxSamples = std::min(std::max(maxSamples, 0), 65536);
    int chans = (ctx && ctx->engine && tapID)
              ? std::max(1, engine::tapChans(ctx->engine, tapID)) : 1;
    maxSamples -= maxSamples % chans;

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

    std::string panel;
    {
        std::lock_guard<std::mutex> lock(ui->mtx);
        panel = ui->currentPanel;
    }
    std::uint64_t id = bindWaveformWidget(ui, panel, name, path.c_str());
    if (id == 0)
        std::fprintf(stderr, "ui.waveform: cannot read \"%s\"\n", path.c_str());
    vm.reg(dst).i = static_cast<i64>(id);
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

// fn uiSpectrum(name String, node Int, outlet Int, silo Int) Int
static void ffi_uiSpectrum(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* name = regString(vm, argBase);
    i64 node = vm.reg(argBase + 1).i;
    int outlet = static_cast<int>(vm.reg(argBase + 2).i);
    int silo = static_cast<int>(vm.reg(argBase + 3).i);
    vm.reg(dst).i = makeTapWidget(vm, name, UIWidgetKind::Spectrum, node, outlet, silo);
}

// fn uiMasterTap(name String, kind Int) Int
// kind: 0 = Meter, 1 = Scope, 2 = Spectrum. Taps the master output bus
// (post-limiter, post-gain) instead of a node outlet.
static void ffi_uiMasterTap(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* name = regString(vm, argBase);
    i64 which = vm.reg(argBase + 1).i;
    UIWidgetKind kind = which == 0 ? UIWidgetKind::Meter
                      : which == 1 ? UIWidgetKind::Scope
                                   : UIWidgetKind::Spectrum;
    vm.reg(dst).i = makeTapWidget(vm, name, kind, /*nodeID=*/-1, 0, 0);
}

// ---------------------------------------------------------------------------
// Synthdef-derived widgets (materialize a node's interface)
// ---------------------------------------------------------------------------

// Create a widget for one ControlDesc of `nodeID` in the current panel and
// bind it fast-path (shared logic in tzpl_ui_node_controls). Requires
// ui->mtx NOT held. Returns the widget id (0 on failure).
static i64 widgetForControl(ts::VM& vm, i64 nodeID, int silo,
                            engine::ControlDesc const& c) {
    UIState* ui = getUIState(vm);
    if (!ui) return 0;
    std::string panel;
    {
        std::lock_guard<std::mutex> lock(ui->mtx);
        panel = ui->currentPanel;
    }
    return static_cast<i64>(bindControlWidget(ui, panel, c, nodeID, silo));
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
    ts::Vec<ts::Type*> fArgsV; fArgsV.push_back(FloatArray);
    ts::Type* FnVec = reinterpret_cast<ts::Type*>(compiler.functionType(fArgsV, Void));
    // fn(Int, Int, Float) Void for onCell handlers (row, col, value).
    ts::Vec<ts::Type*> fArgsC;
    fArgsC.push_back(Int); fArgsC.push_back(Int); fArgsC.push_back(Float);
    ts::Type* FnCell = reinterpret_cast<ts::Type*>(compiler.functionType(fArgsC, Void));
    ts::Type* StringArray = reinterpret_cast<ts::Type*>(compiler.arrayType(String));

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
    reg("uiRange",        Int,  {String, Float, Float, Float, Float, Int, Float}, ffi_uiRange);
    reg("uiNumber",       Int,  {String, Float},                ffi_uiNumber);
    reg("uiButton",       Int,  {String},                       ffi_uiButton);
    reg("uiToggle",       Int,  {String, Bool},                 ffi_uiToggle);
    reg("uiXY",           Int,  {String, Float, Float, Float, Int, Float,
                                         Float, Float, Float, Int, Float}, ffi_uiXY);
    reg("uiMultiSlider",  Int,  {String, Int, Float, Float, Float, Int, Float}, ffi_uiMultiSlider);
    reg("uiMatrix",       Int,  {String, Int, Int},             ffi_uiMatrix);
    reg("uiButtonMatrix", Int,  {String, Int, Int, Bool},       ffi_uiButtonMatrix);
    reg("uiPianoRoll",    Int,  {String, Float, Int},           ffi_uiPianoRoll);
    reg("uiLabel",        Int,  {String, String},               ffi_uiLabel);
    reg("uiOnChange",     Void, {Int, FnFloat},                 ffi_uiOnChange);
    reg("uiOnChangeXY",   Void, {Int, FnFloat2},                ffi_uiOnChangeXY);
    reg("uiOnChangeVec",  Void, {Int, FnVec},                   ffi_uiOnChangeVec);
    reg("uiOnCell",       Void, {Int, FnCell},                  ffi_uiOnCell);
    reg("uiCellLabels",   StringArray, {Int},                   ffi_uiCellLabels);
    reg("uiSetCellLabels", Void, {Int, StringArray},            ffi_uiSetCellLabels);
    reg("uiSetCellLabel", Void, {Int, Int, Int, String},        ffi_uiSetCellLabel);
    reg("uiBindControl",  Int,  {Int, Int, String, Int},        ffi_uiBindControl);
    reg("uiBindControlY", Int,  {Int, Int, String, Int},        ffi_uiBindControlY);
    reg("uiBindKey",      Void, {Int, String},                  ffi_uiBindKey);
    reg("uiValue",        Float, {Int},                         ffi_uiValue);
    reg("uiValues",       FloatArray, {Int},                    ffi_uiValues);
    reg("uiSetValues",    Void, {Int, FloatArray},              ffi_uiSetValues);
    reg("uiNotes",        FloatArray, {Int},                    ffi_uiNotes);
    reg("uiSetNotes",     Void, {Int, FloatArray},              ffi_uiSetNotes);
    reg("uiSetFrame",     Void, {Int, Float, Float, Float, Float}, ffi_uiSetFrame);
    reg("uiRollRange",    Void, {Int, Int, Int},                ffi_uiRollRange);
    reg("uiShowFloat",    Int,  {String, Float},                ffi_uiShowFloat);
    reg("uiShowBool",     Int,  {String, Bool},                 ffi_uiShowBool);
    reg("uiShowVec",      Int,  {String, FloatArray},           ffi_uiShowVec);
    reg("uiShowString",   Int,  {String, String},               ffi_uiShowString);
    reg("uiValueY",       Float, {Int},                         ffi_uiValueY);
    reg("uiSetValue",     Void, {Int, Float},                   ffi_uiSetValue);
    reg("uiSetValueXY",   Void, {Int, Float, Float},            ffi_uiSetValueXY);
    reg("uiSetRange",     Void, {Int, Float, Float},            ffi_uiSetRange);
    reg("uiRemove",       Void, {Int},                          ffi_uiRemove);
    reg("uiClear",        Void, {},                             ffi_uiClear);
    reg("uiMeter",        Int,  {String, Int, Int, Int},        ffi_uiMeter);
    reg("uiScope",        Int,  {String, Int, Int, Int},        ffi_uiScope);
    reg("uiSpectrum",     Int,  {String, Int, Int, Int},        ffi_uiSpectrum);
    reg("uiMasterTap",    Int,  {String, Int},                  ffi_uiMasterTap);
    reg("uiPlot",         Int,  {String, FloatArray},           ffi_uiPlot);
    reg("uiSetData",      Void, {Int, FloatArray},              ffi_uiSetData);
    reg("uiWaveform",     Int,  {String, Int, Int},             ffi_uiWaveform);
    reg("uiTapPeak",      Float, {Int},                         ffi_uiTapPeak);
    reg("uiTapRms",       Float, {Int},                         ffi_uiTapRms);
    reg("uiTapChans",     Int,  {Int},                          ffi_uiTapChans);
    reg("uiTapSamples",   FloatArray, {Int, Int},               ffi_uiTapSamples);
    reg("uiControl",      Int,  {Int, String, Int},             ffi_uiControl);
    reg("uiControls",     IntArray, {Int, Int},                 ffi_uiControls);
}

void registerUIRootScanner(ts::NRTVM& nrtvm, UIState& ui) {
    nrtvm.vm.addExtraRootScanner([&ui](ts::TracingGC& gc) {
        std::lock_guard<std::mutex> lock(ui.mtx);
        for (auto& w : ui.widgets) {
            if (w->onChange) gc.mark(static_cast<ts::GCObj*>(w->onChange));
            if (w->onCell) gc.mark(static_cast<ts::GCObj*>(w->onCell));
        }
    });
}

} // namespace bridge
