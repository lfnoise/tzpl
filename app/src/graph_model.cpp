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
//  graph_model.cpp
//  app
//

#include "graph_model.hpp"

#include <algorithm>

namespace graph {

static PortVM portVM(engine::PortDesc const& p) {
    return {p.name, p.type.chans, (int)p.type.elem, (int)p.type.rate};
}

void buildViewModel(engine::GraphDesc const& g, int silo,
                    std::function<bool(std::string const&, engine::DefDesc&)> lookupDef,
                    GraphViewModel& vm) {
    GraphViewModel out;
    out.generation = g.generation;
    out.silo = silo;

    out.nodes.reserve(g.nodes.size());
    for (auto const& n : g.nodes) {
        NodeVM node;
        node.nodeID = n.nodeID;
        node.defName = n.defName;
        engine::DefDesc def;
        if (lookupDef(n.defName, def)) {
            node.ins.reserve(def.ins.size());
            for (auto const& p : def.ins) node.ins.push_back(portVM(p));
            node.outs.reserve(def.outs.size());
            for (auto const& p : def.outs) node.outs.push_back(portVM(p));
        } else {
            node.defMissing = true;
        }
        out.nodes.push_back(std::move(node));
    }

    // Preserve any prior layout state for nodes that persist across
    // rebuilds (positions are re-derived by the layout pass, but keeping
    // placedByUser/x/y here avoids a visible jump before it runs).
    for (auto& node : out.nodes) {
        int prev = vm.indexOfNode(node.nodeID);
        if (prev >= 0 && vm.silo == silo) {
            NodeVM const& p = vm.nodes[prev];
            node.x = p.x; node.y = p.y;
            node.placedByUser = p.placedByUser;
        }
    }

    out.edges.reserve(g.conns.size());
    for (auto const& c : g.conns) {
        int si = out.indexOfNode(c.srcNode);
        int di = out.indexOfNode(c.dstNode);
        if (si < 0 || di < 0) continue; // defensive: dangling endpoint

        NodeVM& sn = out.nodes[si];
        NodeVM& dn = out.nodes[di];
        // For defMissing nodes, synthesize unnamed pins up to the indices
        // the edges reference.
        if (sn.defMissing && c.srcPort >= (int)sn.outs.size())
            sn.outs.resize(c.srcPort + 1);
        if (dn.defMissing && c.dstPort >= (int)dn.ins.size())
            dn.ins.resize(c.dstPort + 1);
        if (c.srcPort < 0 || c.srcPort >= (int)sn.outs.size()) continue;
        if (c.dstPort < 0 || c.dstPort >= (int)dn.ins.size()) continue;
        out.edges.push_back({si, c.srcPort, di, c.dstPort});
    }

    vm = std::move(out);
}

bool GraphPoller::poll(int silo, GraphViewModel& vm) {
    if (!engine_) return false;

    unsigned long long gen = engine::graphGeneration(engine_);
    if (gen == lastGen_ && silo == lastSilo_) return false;

    engine::GraphDesc g;
    if (!engine::getGraphDesc(engine_, silo, g)) return false;

    if (g.generation != lastGen_) {
        // Defs may have been (re)registered -- drop the metadata cache.
        defCache_.clear();
        defKnown_.clear();
    }

    buildViewModel(g, silo,
        [this](std::string const& name, engine::DefDesc& def) {
            auto known = defKnown_.find(name);
            if (known != defKnown_.end()) {
                if (!known->second) return false;
                def = defCache_[name];
                return true;
            }
            bool ok = engine::getDefDesc(engine_, name.c_str(), def);
            defKnown_[name] = ok;
            if (ok) defCache_[name] = def;
            return ok;
        },
        vm);

    lastGen_ = g.generation;
    lastSilo_ = silo;
    return true;
}

} // namespace graph
