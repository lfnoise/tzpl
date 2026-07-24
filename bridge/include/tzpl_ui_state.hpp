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
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
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
    bool contains(double v) const {
        double a = lo < hi ? lo : hi, b = lo < hi ? hi : lo;
        return a <= v && v <= b;
    }
};

// True if `panel` belongs to the panel-cell root `root`: the exact name
// or a sub-panel "root/...". Sub-panels render as tabs inside the
// root's panel cell; everywhere else (save, history capture, floating
// skip, close removal) they follow their root.
inline bool panelUnderRoot(std::string const& panel,
                           std::string const& root) {
    if (panel == root) return true;
    return panel.size() > root.size() + 1
        && panel.compare(0, root.size(), root) == 0
        && panel[root.size()] == '/';
}

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
    MultiSlider, // values = N bars mapped through spec
    Matrix,      // values = rows*cols cells (row-major, 0/1 toggles)
    PianoRoll,   // noteData = (pitch, startBeat, durBeats) triplets
    Label,       // static text (labelText)
    Range,       // two-ended slider: values[0] = lo, values[1] = hi (mapped)
    ButtonMatrix,// values = rows*cols labeled buttons (momentary or toggle)
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
    // Last-upsert order: bumped on every constructor call, create OR
    // adopt, so a re-run re-stamps widgets in its call order. Panel
    // flow lays unpinned widgets out by this. Runtime-only.
    std::uint64_t seq = 0;

    // Last hover-wheel adjustment time (sliders); wheel bursts commit
    // one history entry when this goes idle. Runtime-only.
    double wheelTime = 0.0;
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
    ts::Obj* onCell = nullptr;              // fn(row, col, v), GC-rooted too
    // Host-side click action (Button widgets): set by the app, invoked by
    // the renderer on press (async, off ui->mtx) -- e.g. the graph view's
    // buffer "load" buttons open a file dialog. Never serialized.
    std::function<void()> hostAction;

    // Event state: set by the GUI thread on user interaction, consumed by
    // the per-frame dispatch (fast path) and callback delivery.
    bool dirtyEngine = false;
    bool dirtyCallback = false;

    // Gesture tracking (GUI thread only): a continuous interaction (slider
    // drag, XY sweep) sets gestureEnded once on release, so the notebook
    // commits ONE history node per gesture.
    bool gestureActive = false;
    bool gestureEnded = false;

    // Layout frame in panel-cell coordinates (arrange mode; document
    // state). fx < 0 = unplaced (auto-flow assigns a slot on first draw
    // in an arranged panel); fw/fh <= 0 = the widget's default size.
    float fx = -1.0f, fy = -1.0f, fw = 0.0f, fh = 0.0f;

    // Matrix/ButtonMatrix dimensions (values is rows*cols, row-major).
    int rows = 1, cols = 1;

    // ButtonMatrix: momentary cells (1 while held, 0 on release) instead
    // of click-to-toggle. Document state (saved in .tzd).
    bool momentary = false;

    // Per-cell labels, row-major; may be shorter than rows*cols (missing
    // entries draw blank). Document state. labelsVersion bumps on every
    // label change so retained-mode GUIs know to repaint.
    std::vector<std::string> cellLabels;
    std::uint64_t labelsVersion = 0;

    // Per-cell interaction events (Matrix/ButtonMatrix): ordered
    // (cellIndex, value) press/flip/release edges appended by the GUI
    // thread, drained in order by the per-frame dispatch into the onCell
    // callback. Unlike the coalesced dirty flags, these preserve both
    // edges of a within-frame click and the ordering of multi-cell
    // changes. Runtime-only.
    std::vector<std::pair<int, double>> cellEvents;

    // GUI-thread-only: cell currently held during a momentary gesture.
    int activeCell = -1;

    // Append a cell event (caller holds UIState::mtx). Dropped when no
    // onCell handler is bound; capped as a safety valve if dispatch
    // stalls (oldest events go first).
    void pushCellEvent(int index, double value) {
        if (!onCell) return;
        if (cellEvents.size() >= 1024) cellEvents.erase(cellEvents.begin());
        cellEvents.push_back({index, value});
    }

    // Piano roll: flat (pitch, startBeat, durBeats) triplets + view range.
    // Pitch is in steps of 1/rollEdo octave: edo 12 = MIDI note numbers,
    // 1200 = cents, 1 = octaves. Fractional pitches are legal and drawn
    // unquantized; the click grid adds whole steps.
    std::vector<float> noteData;
    float rollBeats = 4.0f;
    int rollLowPitch = 48;
    int rollRows = 24;      // vertical extent in steps
    int rollEdo = 12;       // equal divisions of the octave

    // Label text (Label kind).
    std::string labelText;

    // Key binding (ui.bindKey): a single character 'a'-'z' / '0'-'9' or
    // "space". Buttons fire momentary, toggles flip, when no text field
    // has input focus. Empty = unbound. Document state (saved in .tzd).
    std::string keyChord;

    // Engine tap (Meter/Scope). tapID 0 = none. The tap is installed on
    // tapSilo's RT tap table; removing the widget untaps it.
    long tapID = 0;
    int tapSilo = 0;

    // GUI-thread-only scope display state. scopeRing holds interleaved
    // frames of scopeChans channels (frame-aligned from tap creation);
    // scopeChannel selects the displayed channel (-1 = all, stacked).
    std::vector<float> scopeRing;
    int scopeChans = 1;
    int scopeChannel = -1;

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
    std::uint64_t nextSeq = 1;

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
