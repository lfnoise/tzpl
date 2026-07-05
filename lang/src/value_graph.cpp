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
//  value_graph.cpp
//  lang
//
//  Shared state and root dispatch for cycle-safe deep traversals.
//

#include "value_graph.hpp"

#include <cstdlib>
#include <format>
#include <stdexcept>

namespace ts {

thread_local GraphFastState* gEqFast       = nullptr;
thread_local GraphEqCtx*     gGraphEqCtx   = nullptr;
thread_local GraphFastState* gHashFast     = nullptr;
thread_local GraphHashCtx*   gGraphHashCtx = nullptr;

i64 graphFastFuel(i64 defaultFuel) {
    static i64 const envFuel = [] {
        char const* e = std::getenv("TZPL_GRAPH_FUEL");
        return e ? (i64)std::atoll(e) : (i64)-1;
    }();
    return envFuel > 0 ? envFuel : defaultFuel;
}

// --- EquivalenceSet ---

u32 EquivalenceSet::findRoot(u32 i) {
    while (nodes_[i].parent != i) {
        nodes_[i].parent = nodes_[nodes_[i].parent].parent;  // path halving
        i = nodes_[i].parent;
    }
    return i;
}

u32 EquivalenceSet::indexOf(Obj* o) {
    auto it = index_.find(o);
    if (it != index_.end()) return it->second;
    u32 idx = (u32)nodes_.size();
    nodes_.push_back({idx, 1});
    index_.emplace(o, idx);
    return idx;
}

bool EquivalenceSet::unionFind(Obj* a, Obj* b) {
    u32 ra = findRoot(indexOf(a));
    u32 rb = findRoot(indexOf(b));
    if (ra == rb) return true;
    if (nodes_[ra].size < nodes_[rb].size) std::swap(ra, rb);
    nodes_[rb].parent = ra;                    // union by size
    nodes_[ra].size += nodes_[rb].size;
    return false;
}

// --- traversal-neutral user-code execution ---

GraphNeutralScope::GraphNeutralScope()
    : eqFast_(gEqFast)
    , eqCtx_(gGraphEqCtx)
    , hashFast_(gHashFast)
    , hashCtx_(gGraphHashCtx)
{
    gEqFast = nullptr;
    gGraphEqCtx = nullptr;
    gHashFast = nullptr;
    gGraphHashCtx = nullptr;
}

GraphNeutralScope::~GraphNeutralScope() {
    gEqFast = eqFast_;
    gGraphEqCtx = eqCtx_;
    gHashFast = hashFast_;
    gGraphHashCtx = hashCtx_;
}

void graphForceListNode(ListNode* node, i64& forces, char const* op) {
    if (!node->isLazy_) return;
    if (++forces > kLazyForceLimit) {
        throw std::runtime_error(std::format(
            "{}: lazy List exceeded the {} element force limit "
            "(unbounded list?)", op, kLazyForceLimit));
    }
    // The generator is arbitrary user code; it must not see (or corrupt)
    // the traversal in flight.
    GraphNeutralScope neutral;
    node->force(*gCurrentVM);
}

// --- root dispatch: fast attempt, slow fallback ---

namespace {

struct EqFastScope {
    GraphFastState state;
    explicit EqFastScope(i64 fuel) : state{fuel, 0} { gEqFast = &state; }
    ~EqFastScope() { gEqFast = nullptr; }
};

struct EqSlowScope {
    GraphEqCtx ctx;
    EqSlowScope() { gGraphEqCtx = &ctx; }
    ~EqSlowScope() { gGraphEqCtx = nullptr; }
};

} // namespace

bool graphEqualRootWord(Word a, Word b, Type* type) {
    try {
        EqFastScope fast(graphFastFuel(kEqualFastFuel));
        return WordEqual{type}.eqFast(a, b);
    } catch (GraphFuelExhausted const&) {
        // Huge value or a cycle: restart from the root, cycle-safe.
    }
    EqSlowScope slow;
    return graphEqualSlowWord(a, b, type, slow.ctx);
}

bool graphEqualRootWords(Word const* a, Word const* b, Type* type) {
    try {
        EqFastScope fast(graphFastFuel(kEqualFastFuel));
        return wordsEqualFast(a, b, type);
    } catch (GraphFuelExhausted const&) {
        // Huge value or a cycle: restart from the root, cycle-safe.
    }
    EqSlowScope slow;
    return graphEqualSlowWords(a, b, type, slow.ctx);
}

namespace {

struct HashFastScope {
    GraphFastState state;
    explicit HashFastScope(i64 fuel) : state{fuel, 0} { gHashFast = &state; }
    ~HashFastScope() { gHashFast = nullptr; }
};

struct HashSlowScope {
    GraphHashCtx ctx;
    HashSlowScope() { gGraphHashCtx = &ctx; }
    ~HashSlowScope() { gGraphHashCtx = nullptr; }
};

} // namespace

size_t graphHashRootWord(Word w, Type* type) {
    try {
        HashFastScope fast(graphFastFuel(kHashFastFuel));
        return WordHash{type}.hashFast(w);
    } catch (GraphFuelExhausted const&) {
        // Huge value or a cycle: restart from the root, cycle-safe.
    }
    HashSlowScope slow;
    return graphHashSlowWord(w, type, slow.ctx);
}

size_t graphHashRootWords(Word const* base, Type* type) {
    try {
        HashFastScope fast(graphFastFuel(kHashFastFuel));
        return hashWordsFast(base, type);
    } catch (GraphFuelExhausted const&) {
        // Huge value or a cycle: restart from the root, cycle-safe.
    }
    HashSlowScope slow;
    return graphHashSlowWords(base, type, slow.ctx);
}

} // namespace ts
