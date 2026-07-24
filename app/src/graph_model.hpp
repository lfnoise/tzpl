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
//  graph_model.hpp
//  app
//
//  Toolkit-free view-model for the audio graph display: polls the
//  engine's topology shadow (generation-gated, one atomic read when
//  nothing changed) and joins live nodes with their DefDesc port
//  metadata. No GUI-framework includes -- shared by the JUCE view and
//  a future ImGui/imnodes view.
//

#ifndef graph_model_hpp
#define graph_model_hpp

#include "tzpl_client_interface.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace graph {

// One port (inlet or outlet) resolved from the node's DefDesc.
struct PortVM {
    std::string name;
    int chans = 0;
    int elem = 0; // tzpl_ElemType
    int rate = 0; // tzpl_Rate
};

struct NodeVM {
    long long nodeID = 0;
    std::string defName;
    std::vector<PortVM> ins;
    std::vector<PortVM> outs;
    // Def lookup failed (e.g. superseded without replacement). The node is
    // still drawn; pins are synthesized from edge endpoints.
    bool defMissing = false;

    // Layout slots. The view measures w/h from its font; autoLayout (or a
    // stored user position) fills x/y.
    float x = 0, y = 0, w = 0, h = 0;
    bool placedByUser = false;
};

// Endpoints are INDICES into GraphViewModel::nodes, plus a port index.
struct EdgeVM {
    int srcNode = 0; int srcPort = 0;
    int dstNode = 0; int dstPort = 0;
};

struct GraphViewModel {
    std::vector<NodeVM> nodes; // sorted by nodeID
    std::vector<EdgeVM> edges;
    unsigned long long generation = 0;
    int silo = 0;

    int indexOfNode(long long nodeID) const {
        for (int i = 0; i < (int)nodes.size(); ++i)
            if (nodes[i].nodeID == nodeID) return i;
        return -1;
    }
};

class GraphPoller {
public:
    explicit GraphPoller(engine::Engine* e) : engine_(e) {}

    // Cheap when nothing changed (one atomic read). Rebuilds `vm` and
    // returns true when the engine's graph generation moved or `silo`
    // differs from the last build.
    bool poll(int silo, GraphViewModel& vm);

    // Force the next poll to rebuild (e.g. when the view is shown).
    void invalidate() { lastGen_ = 0; lastSilo_ = -1; }

private:
    engine::Engine* engine_;
    unsigned long long lastGen_ = 0;
    int lastSilo_ = -1;
    // defName -> DefDesc, cleared whenever the generation moves (defs may
    // have been hot-reloaded). Negative results cached as defMissing.
    std::unordered_map<std::string, engine::DefDesc> defCache_;
    std::unordered_map<std::string, bool> defKnown_;
};

// Build a view-model from an already-fetched GraphDesc + a def lookup.
// Split out of GraphPoller for headless testing.
void buildViewModel(engine::GraphDesc const& g, int silo,
                    std::function<bool(std::string const&, engine::DefDesc&)> lookupDef,
                    GraphViewModel& vm);

} // namespace graph

#endif /* graph_model_hpp */
