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
//  graph_layout.hpp
//  app
//
//  Auto-layout for the audio graph display: layered left-to-right DAG
//  toward Audio Out (nodeID 0), with session-sticky user positions
//  overriding the computed ones. Pure functions over the view-model --
//  no GUI-framework or engine dependencies beyond graph_model.
//

#ifndef graph_layout_hpp
#define graph_layout_hpp

#include "graph_model.hpp"

#include <map>
#include <utility>

namespace graph {

// Session-only node positions, keyed by (silo, nodeID). Dragged nodes are
// recorded here and keep their position across graph rebuilds until
// cleared. Persistence is deferred to session management (Phase 15).
struct LayoutStore {
    std::map<std::pair<int, long long>, std::pair<float, float>> pos;

    bool get(int silo, long long nodeID, float& x, float& y) const {
        auto it = pos.find({silo, nodeID});
        if (it == pos.end()) return false;
        x = it->second.first;
        y = it->second.second;
        return true;
    }
    void set(int silo, long long nodeID, float x, float y) {
        pos[{silo, nodeID}] = {x, y};
    }
    void clearSilo(int silo) {
        std::erase_if(pos, [silo](auto const& kv) {
            return kv.first.first == silo;
        });
    }
};

struct LayoutMetrics {
    float colGap = 60.f;
    float rowGap = 24.f;
    float margin = 40.f; // top/left inset of the layout area
};

// Compute x/y for every node in `vm` whose position is not overridden by
// `store`. Reads each node's w/h (the view measures them first).
//
// Algorithm: longest-path layering by reverse DFS from Audio Out (node 0);
// cycle back-edges are layer-neutral (cycles are legal in the engine --
// one-sample delay). Nodes with no path to Audio Out are placed by a
// forward pass instead: one column right of their rightmost source, so
// their input edges never run backwards; unconnected sources land in the
// leftmost column, while an unconnected sink (an idle Audio Out) is
// parked in its own column past the right edge. Columns run
// left-to-right toward Audio Out; rows are
// ordered by one barycenter pass over the previous column, ties broken
// by nodeID.
void autoLayout(GraphViewModel& vm, LayoutStore const& store,
                LayoutMetrics const& m = {});

} // namespace graph

#endif /* graph_layout_hpp */
