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
//  tzpl_ui_node_controls.hpp
//  bridge
//
//  Node-control widgets: materialize a live node's control interface
//  (from its def's ControlSpecs) as engine-bound UIState widgets. Shared
//  by the `ui` module FFI (ui.control / ui.controls) and the app's graph
//  view (double-click a node); both apps' widget renderers and the
//  ControlsDispatcher then handle drawing and coalesced setControl
//  delivery.
//

#ifndef tzpl_ui_node_controls_hpp
#define tzpl_ui_node_controls_hpp

#include "tzpl_ui_state.hpp"
#include "tzpl_client_interface.hpp"

#include <string>

namespace bridge {

// Convert an ABI control spec (from a loaded synthdef) to a UISpec.
// ABI warp ordinal = lang ordinal + 1; 0 (None) means unspecified ->
// linear.
UISpec specFromControl(tzpl_ControlSpec const& cs);

// Widget kind for a control kind: Continuous -> Slider, Trigger ->
// Button, Boolean -> Toggle, Select -> Number.
UIWidgetKind widgetKindForControl(tzpl_ControlKind kind);

// Upsert one widget for `c` in `panel`, bound fast-path to the node's
// control (an existing widget keeps its value but rebinds). Requires
// ui->mtx NOT held. Returns the widget id.
std::uint64_t bindControlWidget(UIState* ui, std::string const& panel,
                                engine::ControlDesc const& c,
                                long long nodeID, int silo);

// Materialize the whole control interface of a node of def `defName`
// into `panel`. Returns the number of widgets bound: 0 when the def has
// no controls, -1 when the def is unknown. The caller supplies the def
// name (the graph view reads it from the topology shadow; the FFI path
// resolves it via AppContext::nodeDefNames).
int materializeNodeControls(UIState* ui, engine::Engine* e,
                            std::string const& panel,
                            long long nodeID, int silo,
                            char const* defName);

// Upsert a tap-backed widget (Meter, Scope or Spectrum) in `panel` and
// install the engine tap behind it. `nodeID` < 0 taps the master output bus
// instead of a node outlet (silo is then forced to 0). An existing widget of
// the same (panel, name) is retapped, its old tap released. Requires
// ui->mtx NOT held (bundle submit takes the engine's NRT lock).
//
// Returns the widget id, 0 only when `ui` or `e` is null. If the engine
// refuses the tap -- tzpl_errResourceLimit when the silo's tap table is full
// -- the widget still exists but holds no tap, and *engineErr (when given)
// carries the error for the caller to surface.
std::uint64_t bindTapWidget(UIState* ui, engine::Engine* e,
                            std::string const& panel, std::string const& name,
                            UIWidgetKind kind, long long nodeID, int outlet,
                            int silo, int* engineErr = nullptr);

// Upsert a Waveform widget showing a min/max overview of the audio file
// at `path` (channel 0). Used for buffer displays (ui.waveform and the
// graph view's buffer rows). Requires ui->mtx NOT held. Returns the
// widget id, or 0 if the file cannot be read.
std::uint64_t bindWaveformWidget(UIState* ui, std::string const& panel,
                                 std::string const& name, char const* path);

} // namespace bridge

#endif /* tzpl_ui_node_controls_hpp */
