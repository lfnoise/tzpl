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
//  graph_meters.hpp
//  app
//
//  Toolkit-free per-outlet level meters for the graph view. Owns the engine
//  taps behind them: enable() installs one, disable()/prune()/clear() remove
//  them. No GUI includes -- shared by the JUCE view and a future ImGui view.
//
//  Meters are OPT-IN per node (right-click > Meter) and SESSION-ONLY: they
//  are never persisted. Taps are a scarce RT resource tied to live audio, and
//  restoring them from a document would mean tapping nodes that may not exist
//  yet. Hiding the view keeps them; closing it drops them.
//
//  Threading: message/GUI thread only. Bundle submission is thread-local and
//  validates under the engine's NRT lock, so calling from the message thread
//  is fine; do not call with UIState::mtx held.
//

#ifndef graph_meters_hpp
#define graph_meters_hpp

#include "graph_model.hpp"

#include <map>

namespace graph {

// One metered outlet.
struct MeterKey {
    int silo = 0;
    long long nodeID = 0;
    int outlet = 0;
    auto operator<=>(MeterKey const&) const = default;
};

struct MeterState {
    long long tapID = 0;
    float rms = 0.f;
    float peak = 0.f;
    // Peak with a slow fall, so the bar stays readable between polls.
    float peakHold = 0.f;
};

class MeterStore {
public:
    // Returns 0, or the engine error. tzpl_errResourceLimit means the silo's
    // tap table is full -- surface it, don't retry.
    int enable(engine::Engine* e, MeterKey k);
    void disable(engine::Engine* e, MeterKey k);
    bool enabled(MeterKey k) const;

    // Every f32 outlet of a node, as individual taps. Non-f32 outlets are
    // skipped (the engine only taps f32). Returns the first error, or 0.
    int enableNode(engine::Engine* e, int silo, GraphViewModel const& vm,
                   long long nodeID);
    void disableNode(engine::Engine* e, int silo, long long nodeID);
    bool nodeEnabled(int silo, long long nodeID) const;

    // Drop meters whose node is gone from `vm`. Needed on every topology
    // change: the engine clears a dead node's RT tap entry but leaves the
    // registry slot alive reading silence, so nothing else would free it.
    void prune(engine::Engine* e, GraphViewModel const& vm);
    void clearSilo(engine::Engine* e, int silo);
    void clear(engine::Engine* e);

    // Refresh levels from the engine. `holdDecay` multiplies peakHold each
    // call (choose it from the caller's tick rate).
    void poll(engine::Engine* e, float holdDecay);

    MeterState const* find(MeterKey k) const;
    int count() const { return (int)meters_.size(); }

private:
    std::map<MeterKey, MeterState> meters_;
};

} // namespace graph

#endif /* graph_meters_hpp */
