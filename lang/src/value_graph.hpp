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
//  value_graph.hpp
//  lang
//
//  Shared infrastructure for cycle-safe deep traversals (equality, hashing,
//  printing, serialization) over possibly-cyclic value graphs.
//
//  Heap objects with in-place mutation (ObjArray/InlineArray, MapObj/SetObj,
//  RefValue/InlineRef) can form reference cycles, so every deep traversal
//  needs a termination strategy. The scheme, per operation:
//
//    * A FAST path -- the pre-existing naive recursion -- runs first with a
//      fuel budget. Acyclic values of ordinary size finish well within the
//      budget and pay only a counter decrement per visited node.
//    * When the fuel runs out (a huge value or a cycle), the traversal
//      RESTARTS from the root on a SLOW path that carries explicit
//      cycle-handling state: a union-find equivalence set for ==, a visited
//      memo for hash.
//
//  The slow equality path implements bisimulation semantics: two cyclic
//  values are equal if their infinite unrollings are equal (Adams & Dybvig,
//  "Efficient Nondestructive Equality Checking for Trees and Graphs").
//

#ifndef value_graph_hpp
#define value_graph_hpp

#include "value.hpp"

namespace ts {

// Fuel budgets for the allocation-free fast paths. One unit is charged per
// visited node; exhaustion aborts the fast attempt and restarts on the slow
// path (worst case re-does ~this much work, which is noise for a value big
// enough to exhaust it).
inline constexpr i64 kEqualFastFuel = 4096;
inline constexpr i64 kHashFastFuel  = 4096;

// Slow-path recursion depth limit: deeper graphs raise a runtime error
// instead of overflowing the C++ stack.
inline constexpr u32 kGraphMaxDepth = 10000;

// Cap on LAZY ListNode forces per ==/hash/serialize traversal, so an
// unbounded lazy list raises a runtime error instead of hanging. Walking
// already-forced nodes is not charged.
inline constexpr i64 kLazyForceLimit = 10000;

// Returns `defaultFuel`, or TZPL_GRAPH_FUEL from the environment if set.
// (Test knob: a tiny value forces the slow paths across the whole suite.)
i64 graphFastFuel(i64 defaultFuel);

// Thrown by the fast-path fuel ticks; caught only by the root entry that
// armed the fast attempt. Carries no state and never allocates.
struct GraphFuelExhausted {};

// Union-find over Obj* identities (Adams & Dybvig). unionFind(a, b) returns
// true when a and b are already known equivalent; otherwise it merges their
// classes and returns false ("assume equal, keep verifying") -- the
// coinductive step that makes comparing cyclic graphs terminate.
class EquivalenceSet {
    struct Node { u32 parent; u32 size; };
    Vec<Node> nodes_;                // index-stable arena, no per-node alloc
    Map<Obj*, u32> index_;
public:
    EquivalenceSet()
        : nodes_(rt::STLAllocator<Node>(rt::gCurrentAllocator))
        , index_(0, std::hash<Obj*>{}, std::equal_to<Obj*>{},
                 rt::STLAllocator<std::pair<Obj* const, u32>>(rt::gCurrentAllocator)) {}

    bool unionFind(Obj* a, Obj* b);

private:
    u32 findRoot(u32 i);             // iterative, path halving
    u32 indexOf(Obj* o);
};

// Per-root state of an armed fast attempt.
struct GraphFastState {
    i64 fuel;
    i64 forces = 0;
};

// Per-root state of an active slow (cycle-safe) equality traversal.
struct GraphEqCtx {
    EquivalenceSet uf;
    u32 depth = 0;
    i64 forces = 0;
};

// Active-traversal registers. Non-null exactly while the corresponding
// traversal runs. Nested public entries (e.g. wordsEqual re-entered through
// MapObj::findSlot while a slow map comparison probes) consult these to
// join the traversal in flight instead of starting a fresh one -- that
// sharing is what makes cyclic keys inside cyclic maps terminate.
extern thread_local GraphFastState* gEqFast;
extern thread_local GraphEqCtx*     gGraphEqCtx;

inline void eqFuelTick() {
    if (gEqFast && --gEqFast->fuel < 0) throw GraphFuelExhausted{};
}

// Root entries: arm the fast path, fall back to the slow path on fuel
// exhaustion. Called by wordsEqual / WordEqual when no traversal is active.
bool graphEqualRootWord(Word a, Word b, Type* type);
bool graphEqualRootWords(Word const* a, Word const* b, Type* type);

// Slow-path entries (graph_equal.cpp). `Word` form for 1-word values of
// non-Inline repr; `Words` form for possibly multi-word payloads.
bool graphEqualSlowWord(Word a, Word b, Type* type, GraphEqCtx& ctx);
bool graphEqualSlowWords(Word const* a, Word const* b, Type* type, GraphEqCtx& ctx);

// Force one list node on behalf of a bounded traversal: charges `forces`
// if the node is actually lazy (runtime error past kLazyForceLimit, naming
// `op`), and neutralizes all graph-traversal registers while the generator
// runs -- it is arbitrary user code that may itself compare/hash values.
void graphForceListNode(ListNode* node, i64& forces, char const* op);

// RAII: save and clear every graph-traversal register while user code runs,
// restore on exit.
class GraphNeutralScope {
    GraphFastState* eqFast_;
    GraphEqCtx*     eqCtx_;
public:
    GraphNeutralScope();
    ~GraphNeutralScope();
    GraphNeutralScope(GraphNeutralScope const&) = delete;
    GraphNeutralScope& operator=(GraphNeutralScope const&) = delete;
};

} // namespace ts

#endif /* value_graph_hpp */
