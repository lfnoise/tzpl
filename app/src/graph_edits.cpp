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
//  graph_edits.cpp
//  app
//

#include "graph_edits.hpp"

#include <set>

namespace graph {

bool canConnect(PortVM const& src, PortVM const& dst, long long dstNodeID) {
    if (src.rate != dst.rate) return false;
    if (src.elem != dst.elem) return false;
    if (dstNodeID != 0 && src.chans != dst.chans) return false;
    return true;
}

long long nextFreeNodeID(GraphViewModel const& vm) {
    std::set<long long> used;
    for (auto const& n : vm.nodes) used.insert(n.nodeID);
    long long id = 2;
    while (used.count(id)) ++id;
    return id;
}

std::string errText(int err) {
    switch (err) {
        case tzpl_errNone:               return "ok";
        case tzpl_errNodeIDAlreadyTaken: return "node ID already taken";
        case tzpl_errNodeDefNotFound:    return "synthdef not found";
        case tzpl_errNodeNotFound:       return "node not found";
        case tzpl_errSiloOutOfRange:     return "silo out of range";
        case tzpl_errInputOutOfRange:    return "input port out of range";
        case tzpl_errOutputOutOfRange:   return "output port out of range";
        case tzpl_errCyclicConnection:   return "cyclic connection";
        case tzpl_errTypeMismatch:       return "element type mismatch";
        case tzpl_errRateMismatch:       return "rate mismatch";
        case tzpl_errChanMismatch:       return "channel count mismatch";
        case tzpl_errNumPortsMismatch:   return "port count mismatch";
        case tzpl_errNoActiveBundle:     return "no active command bundle";
        case tzpl_errTooLate:            return "scheduled too late";
        default:
            return "engine error " + std::to_string(err);
    }
}

// All submitters: begin() opens a fresh thread-local bundle; go() submits
// it to the silo and closes it, success or failure.

int connectNodes(engine::Engine* e, int silo,
                 long long srcNode, int srcPort,
                 long long dstNode, int dstPort,
                 double xfadeTime) {
    if (!e) return tzpl_errInternal;
    int err = engine::begin(e);
    if (err != tzpl_errNone) return err;
    engine::connect({srcNode, srcPort}, {dstNode, dstPort}, xfadeTime);
    return engine::go(silo);
}

int disconnectWire(engine::Engine* e, int silo,
                   long long srcNode, int srcPort,
                   long long dstNode, int dstPort,
                   double xfadeTime) {
    if (!e) return tzpl_errInternal;
    int err = engine::begin(e);
    if (err != tzpl_errNone) return err;
    engine::disconnectSource({srcNode, srcPort}, {dstNode, dstPort}, xfadeTime);
    return engine::go(silo);
}

int createNode(engine::Engine* e, int silo,
               std::string const& defName, long long nodeID) {
    if (!e) return tzpl_errInternal;
    int err = engine::begin(e);
    if (err != tzpl_errNone) return err;
    engine::newNode(defName.c_str(), nodeID);
    return engine::go(silo);
}

int freeGraphNode(engine::Engine* e, int silo, long long nodeID) {
    if (!e) return tzpl_errInternal;
    int err = engine::begin(e);
    if (err != tzpl_errNone) return err;
    engine::freeNode(nodeID);
    return engine::go(silo);
}

int disconnectGraphNode(engine::Engine* e, int silo, long long nodeID) {
    if (!e) return tzpl_errInternal;
    int err = engine::begin(e);
    if (err != tzpl_errNone) return err;
    engine::disconnectNode(nodeID);
    return engine::go(silo);
}

int disconnectAllWires(engine::Engine* e, int silo, GraphViewModel const& vm,
                       long long nodeID, double xfadeTime) {
    if (!e) return tzpl_errInternal;
    int idx = vm.indexOfNode(nodeID);
    if (idx < 0) return tzpl_errNodeNotFound;

    int err = engine::begin(e);
    if (err != tzpl_errNone) return err;
    for (auto const& ed : vm.edges) {
        if (ed.srcNode != idx && ed.dstNode != idx) continue;
        engine::disconnectSource(
            {vm.nodes[ed.srcNode].nodeID, ed.srcPort},
            {vm.nodes[ed.dstNode].nodeID, ed.dstPort}, xfadeTime);
    }
    return engine::go(silo);
}

} // namespace graph
