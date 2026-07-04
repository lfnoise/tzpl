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
//  tzpl_ui_state.hpp
//  bridge
//
//  Shared state for the `ui` lang module: a registry of live control
//  widgets. FFI functions (eval thread, under nrtvm.mtx) create and bind
//  widgets; the GUI thread renders them and dispatches value changes.
//
//  Lock order: nrtvm.mtx BEFORE UIState::mtx. The GC root scanner and the
//  FFI functions both run with nrtvm.mtx held and take UIState::mtx inside
//  it; the GUI thread takes UIState::mtx alone for rendering and the engine
//  fast path, and takes nrtvm.mtx first (try_lock) when delivering onChange
//  callbacks. Never call into the VM (allocate, callCallable) while holding
//  UIState::mtx.
//

#ifndef tzpl_ui_state_hpp
#define tzpl_ui_state_hpp

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ts { struct Obj; }

namespace bridge {

// ---------------------------------------------------------------------------
// Control spec + warp mapping
//
// Mirrors synthdef.x ControlSpec. `warp` ordinals match the lang ControlWarp
// enum (ABI tzpl_ControlWarp minus one). This is the reference implementation
// of the warp semantics: map() takes a normalized position in [0,1] to a
// control value in [lo,hi]; unmap() is its inverse. signedSquare and cubed
// are bipolar (odd around the range midpoint).
// ---------------------------------------------------------------------------

enum class UIWarp : int {
    Linear = 0,
    Exponential,
    Step,          // quantize to multiples of `warpParam` above lo
    SignedSquare,
    Cubed,
};

struct UISpec {
    double lo = 0.0;
    double hi = 1.0;
    double init = 0.0;
    UIWarp warp = UIWarp::Linear;
    double warpParam = 0.0;  // step size for UIWarp::Step

    double map(double pos01) const;    // position [0,1] -> value [lo,hi]
    double unmap(double value) const;  // value -> position [0,1]
    double clamp(double value) const;
};

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

enum class UIWidgetKind : int {
    Slider = 0,
    Number,
    Button,
    Toggle,
    XY,
    Meter,   // engine tap: rms in values[0], peak in values[1]
    Scope,   // engine tap: sample FIFO drained into scopeRing
    Plot,    // static data plot (plotData)
    Waveform,// audio file overview (waveMin/waveMax mipmap)
};

// Engine fast-path binding: on value change the GUI thread sends
// setControl(nodeID, controlID, value) in a per-frame bundle, without
// entering the VM.
struct UIEngineTarget {
    long nodeID = -1;
    long controlID = -1;
    int silo = 0;
};

struct UIWidget {
    std::uint64_t id = 0;
    UIWidgetKind kind = UIWidgetKind::Slider;
    std::string name;      // reconciliation key, unique per panel
    std::string panel;     // panel name ("" = default Controls panel)

    UISpec spec;           // value mapping (X axis for XY)
    UISpec spec2;          // Y axis for XY

    // Current values. Slider/Number/Toggle/Button use values[0];
    // XY uses values[0] (x) and values[1] (y).
    std::vector<double> values;

    // Bindings (behavior; never serialized, cleared on re-run rebinding).
    std::optional<UIEngineTarget> target;   // fast path (X axis for XY)
    std::optional<UIEngineTarget> target2;  // fast path, XY Y axis
    ts::Obj* onChange = nullptr;            // lang closure, GC-rooted by scanner

    // Event state: set by the GUI thread on user interaction, consumed by
    // the per-frame dispatch (fast path) and callback delivery.
    bool dirtyEngine = false;
    bool dirtyCallback = false;

    // Engine tap (Meter/Scope). tapID 0 = none. The tap is installed on
    // tapSilo's RT tap table; removing the widget untaps it.
    long tapID = 0;
    int tapSilo = 0;

    // GUI-thread-only scope display ring (drained from the tap FIFO).
    std::vector<float> scopeRing;

    // Plot data (Plot kind), set by ui.plot / ui.setData.
    std::vector<float> plotData;

    // Waveform overview (Waveform kind): per-bin min/max of channel 0,
    // built by ui.waveform from the buffer's source audio file.
    std::vector<float> waveMin;
    std::vector<float> waveMax;
    std::int64_t waveFrames = 0;
};

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

struct UIState {
    std::mutex mtx;

    // Insertion-ordered for stable drawing; owned here.
    std::vector<std::unique_ptr<UIWidget>> widgets;
    std::uint64_t nextId = 1;

    // Target panel for subsequent widget constructors (set by ui.panel).
    // "" = the default Controls panel.
    std::string currentPanel;

    // Engine tap id allocator (Meter/Scope widgets).
    std::uint64_t nextTapId = 1;

    // ---- All methods below require mtx to be held by the caller. ----

    UIWidget* findByName(std::string const& panel, std::string const& name);
    UIWidget* findById(std::uint64_t id);

    // Get-or-create by (panel, name). An existing widget keeps its values
    // (saved/tweaked state wins over the code's init) but adopts the new
    // kind/spec and has its bindings cleared for fresh rebinding; a new
    // widget starts at the spec's init value(s).
    UIWidget* upsert(std::string const& panel, std::string const& name,
                     UIWidgetKind kind, UISpec const& spec, UISpec const& spec2);

    bool remove(std::uint64_t id);
    void clear();
};

} // namespace bridge

#endif /* tzpl_ui_state_hpp */
