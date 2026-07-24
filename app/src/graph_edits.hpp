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
//  graph_edits.hpp
//  app
//
//  Toolkit-free graph-editing helpers for the interactive graph view:
//  compatibility prechecks mirroring the engine's rules, node-ID
//  allocation, and begin/.../go bundle submitters. Shared by the JUCE
//  view and a future ImGui view. All submitters run on the caller's
//  thread (the GUI message thread is fine -- the bundle is thread-local
//  and go() validates under the engine's NRT lock).
//

#ifndef graph_edits_hpp
#define graph_edits_hpp

#include "graph_model.hpp"

#include <string>

namespace graph {

// Mirror of the engine's connection type rules (Silo::compatibleTypes /
// relaxedCompatibleTypes): rate and element type must match; channel
// counts must match too unless the destination is the Audio Out node
// (nodeID 0), which accepts any channel count. Used to highlight legal
// drop targets while dragging a wire -- the engine still revalidates at
// submit.
bool canConnect(PortVM const& src, PortVM const& dst, long long dstNodeID);

// Smallest nodeID >= 2 not used by any node in `vm` (0/1 are the
// built-in Audio Out / Audio In).
long long nextFreeNodeID(GraphViewModel const& vm);

// Short human-readable message for an engine error code.
std::string errText(int err);

// Crossfade applied to UI connection edits so float signals fade in/out
// instead of jumping (the engine skips the fade for non-float ports).
inline constexpr double kUIXFadeTime = 0.1; // seconds

// Bundle submitters. Each returns tzpl_errNone (0) on success or the
// engine error; on failure the whole bundle was discarded (atomic abort).
int connectNodes(engine::Engine* e, int silo,
                 long long srcNode, int srcPort,
                 long long dstNode, int dstPort,
                 double xfadeTime = kUIXFadeTime);
int disconnectWire(engine::Engine* e, int silo,
                   long long srcNode, int srcPort,
                   long long dstNode, int dstPort,
                   double xfadeTime = kUIXFadeTime);
int createNode(engine::Engine* e, int silo,
               std::string const& defName, long long nodeID);
int freeGraphNode(engine::Engine* e, int silo, long long nodeID);
int disconnectGraphNode(engine::Engine* e, int silo, long long nodeID);
// Disconnect every wire touching `nodeID` (from `vm`, the current view)
// as one bundle of per-wire fading disconnects -- the fading equivalent
// of disconnectGraphNode, which cuts hard.
int disconnectAllWires(engine::Engine* e, int silo, GraphViewModel const& vm,
                       long long nodeID, double xfadeTime = kUIXFadeTime);

} // namespace graph

#endif /* graph_edits_hpp */
