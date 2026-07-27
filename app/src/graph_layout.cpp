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
//  graph_layout.cpp
//  app
//

#include "graph_layout.hpp"

#include <algorithm>
#include <vector>

namespace graph {

void autoLayout(GraphViewModel& vm, LayoutStore const& store,
                LayoutMetrics const& m) {
    int const n = (int)vm.nodes.size();
    if (n == 0) return;

    // Adjacency: for each node, the nodes it feeds (src -> dst).
    std::vector<std::vector<int>> outAdj(n), inAdj(n);
    for (auto const& e : vm.edges) {
        outAdj[e.srcNode].push_back(e.dstNode);
        inAdj[e.dstNode].push_back(e.srcNode);
    }

    // Longest path to Audio Out (node 0): layer(sink) = 0, layer(n) =
    // 1 + max(layer of nodes it feeds). Iterative DFS from the sink over
    // in-edges; an on-stack mark makes cycle back-edges layer-neutral.
    std::vector<int> layer(n, -1);       // -1: unreached
    std::vector<char> state(n, 0);       // 0 unvisited, 1 on stack, 2 done

    int sink = vm.indexOfNode(0);

    struct Frame { int node; size_t next; };
    if (sink >= 0) {
        layer[sink] = 0;
        std::vector<Frame> stack{{sink, 0}};
        state[sink] = 1;
        while (!stack.empty()) {
            Frame& f = stack.back();
            if (f.next < inAdj[f.node].size()) {
                int src = inAdj[f.node][f.next++];
                if (state[src] == 1) continue; // back edge (cycle): skip
                int want = layer[f.node] + 1;
                if (layer[src] < want) {
                    layer[src] = want;
                    if (state[src] == 2) state[src] = 0; // re-relax deeper
                }
                if (state[src] == 0) {
                    state[src] = 1;
                    stack.push_back({src, 0});
                }
            } else {
                state[f.node] = 2;
                stack.pop_back();
            }
        }
    }

    int maxLayer = 0;
    for (int i = 0; i < n; ++i) maxLayer = std::max(maxLayer, layer[i]);

    // Columns left-to-right: reachable nodes at maxLayer - layer (Audio Out
    // rightmost, everything as late as its longest path allows).
    std::vector<int> col(n, -1);
    for (int i = 0; i < n; ++i)
        if (layer[i] >= 0) col[i] = maxLayer - layer[i];

    // Nodes with no path to Audio Out (e.g. an unconnected output) must
    // still sit RIGHT of whatever feeds them, or their input edges run
    // backwards. They can only be fed by placed nodes or by each other
    // (feeding a reachable node would make them reachable), so a forward
    // longest-path pass over in-edges places them: one column right of
    // their rightmost source, column 0 when nothing feeds them (an
    // unconnected Audio In lands there). Same on-stack cycle guard as the
    // reverse pass.
    std::fill(state.begin(), state.end(), 0);
    for (int u = 0; u < n; ++u) {
        if (col[u] >= 0 || state[u] == 2) continue;
        std::vector<Frame> stack{{u, 0}};
        state[u] = 1;
        while (!stack.empty()) {
            Frame& f = stack.back();
            if (f.next < inAdj[f.node].size()) {
                int src = inAdj[f.node][f.next++];
                if (col[src] < 0 && state[src] == 0) {
                    state[src] = 1;
                    stack.push_back({src, 0});
                }
            } else {
                int c = 0;
                for (int src : inAdj[f.node])
                    if (col[src] >= 0) c = std::max(c, col[src] + 1);
                col[f.node] = c;
                state[f.node] = 2;
                stack.pop_back();
            }
        }
    }

    // A sink with nothing plugged into it -- an idle Audio Out is the
    // common case -- has no path to constrain it, so both passes above put
    // it in column 0, on the wrong side of everything the user is about to
    // wire into it. Park those in a column of their own past the right
    // edge, where their first wire will already run left-to-right. Only
    // structural sinks (no outlets at all) qualify; an unconnected source
    // such as Audio In still belongs on the left.
    std::vector<char> parked(n, 0);
    bool anyParked = false;
    for (int i = 0; i < n; ++i) {
        if (!inAdj[i].empty() || !outAdj[i].empty()) continue;
        if (!vm.nodes[i].outs.empty() || vm.nodes[i].defMissing) continue;
        // Audio In (node 1) is a source by role even when it has no outlet
        // to show for it -- the engine gives it none until the device is
        // opened with input channels. Parking it right would put the whole
        // graph on the wrong side of the one node everything starts from.
        if (vm.nodes[i].nodeID == 1) continue;
        parked[i] = 1;
        anyParked = true;
    }

    int numCols = 0;
    for (int i = 0; i < n; ++i)
        if (!parked[i]) numCols = std::max(numCols, col[i] + 1);
    for (int i = 0; i < n; ++i)
        if (parked[i]) col[i] = numCols;    // rightmost, empty of anything else
    if (anyParked) ++numCols;               // all-parked graph: still column 0
    numCols = std::max(numCols, 1);

    std::vector<std::vector<int>> cols(numCols);
    for (int i = 0; i < n; ++i)
        cols[col[i]].push_back(i);

    // Column x positions from per-column max width.
    std::vector<float> colX(numCols, m.margin);
    {
        float x = m.margin;
        for (int c = 0; c < numCols; ++c) {
            colX[c] = x;
            float w = 0;
            for (int i : cols[c]) w = std::max(w, vm.nodes[i].w);
            x += w + m.colGap;
        }
    }

    // Row order: nodeID order in the first column, then one barycenter
    // pass over already-placed neighbors in the previous column.
    std::vector<float> rowCenter(n, 0.f);
    for (int c = 0; c < numCols; ++c) {
        auto& col = cols[c];
        std::sort(col.begin(), col.end(), [&](int a, int b) {
            return vm.nodes[a].nodeID < vm.nodes[b].nodeID;
        });
        if (c > 0) {
            std::vector<std::pair<float, int>> keyed;
            keyed.reserve(col.size());
            for (int i : col) {
                float sum = 0; int cnt = 0;
                for (int nb : outAdj[i]) { sum += rowCenter[nb]; ++cnt; }
                for (int nb : inAdj[i])  { sum += rowCenter[nb]; ++cnt; }
                float key = cnt ? sum / (float)cnt : 1e9f; // no neighbors: bottom
                keyed.push_back({key, i});
            }
            std::stable_sort(keyed.begin(), keyed.end(),
                [&](auto const& a, auto const& b) {
                    if (a.first != b.first) return a.first < b.first;
                    return vm.nodes[a.second].nodeID < vm.nodes[b.second].nodeID;
                });
            for (size_t k = 0; k < keyed.size(); ++k) col[k] = keyed[k].second;
        }
        float y = m.margin;
        for (int i : col) {
            vm.nodes[i].x = colX[c];
            vm.nodes[i].y = y;
            rowCenter[i] = y + vm.nodes[i].h * 0.5f;
            y += vm.nodes[i].h + m.rowGap;
        }
    }

    // User positions override everything.
    for (auto& node : vm.nodes) {
        float x, y;
        if (store.get(vm.silo, node.nodeID, x, y)) {
            node.x = x;
            node.y = y;
            node.placedByUser = true;
        } else {
            node.placedByUser = false;
        }
    }
}

} // namespace graph
