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
//  graph_hash.cpp
//  lang
//
//  Cycle-safe slow-path hashing (visited-map memoization). Mirrors the
//  fast path in value.cpp branch for branch and MUST combine identically:
//  the memo only ever short-circuits pointer-identical subobjects, which
//  the fast path would recompute to the same value, so acyclic values
//  hash bit-identically on both paths.
//
//  Cycles: on entering a heap composite a PRELIMINARY hash (kind salt +
//  non-recursive local data, never pointers) is stored in the memo before
//  the children are combined; a cycle hitting back into the in-progress
//  node absorbs that preliminary value. This keeps cyclic hashes
//  deterministic and structural across separately built isomorphic cycles.
//  Caveat (accepted by design): bisimilar cycles of different unrolled
//  lengths compare == but may hash differently.
//

#include "value_graph.hpp"

#include <stdexcept>

namespace ts {

namespace {

struct DepthGuard {
    GraphHashCtx& ctx;
    explicit DepthGuard(GraphHashCtx& c) : ctx(c) {
        if (++ctx.depth > kGraphMaxDepth) {
            throw std::runtime_error("hash: value graph nesting too deep");
        }
    }
    ~DepthGuard() { --ctx.depth; }
};

// Preliminary (in-progress) hash for a heap composite: kind salt mixed
// with cheap local data. Structural only -- no pointer values -- so
// isomorphic separately-built cycles get identical hashes.
size_t prelim(GCTag tag, size_t local) {
    return hashCombine((size_t)tag + 0x517cc1b727220a95ULL, local);
}

} // namespace

size_t graphHashSlowWords(Word const* a, Type* type, GraphHashCtx& ctx) {
    if (!type) return std::hash<i64>{}(a[0].i);
    if (type->repr_ != Type::Repr::Inline) {
        return graphHashSlowWord(a[0], type, ctx);
    }
    if (type == gCurrentVM->complexType()) {
        return hashCombine(std::hash<f64>{}(a[0].f), std::hash<f64>{}(a[1].f));
    }
    if (type == gCurrentVM->fractionType()) {
        return hashCombine(std::hash<i64>{}(a[0].i), std::hash<i64>{}(a[1].i));
    }
    DepthGuard guard(ctx);
    auto hashFields = [&](auto const& layout, size_t seed) {
        size_t h = seed;
        for (auto const& f : layout) {
            if (!f.type) continue;
            h = hashCombine(h, graphHashSlowWords(a + f.wordOffset, f.type, ctx));
        }
        return h;
    };
    if (auto* tt = dynamic_cast<TupleType*>(type))   return hashFields(tt->layout_, tt->fields_.size());
    if (auto* st = dynamic_cast<StructType*>(type))  return hashFields(st->layout_, std::hash<const void*>{}(st->name_));
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        size_t h = std::hash<i64>{}(a[0].i);
        int which = (int)a[0].i;
        if (which >= 0 && (size_t)which < en->layout_.size()) {
            auto const& f = en->layout_[which];
            if (f.type && f.sizeWords > 0) {
                h = hashCombine(h, graphHashSlowWords(a + f.wordOffset, f.type, ctx));
            }
        }
        return h;
    }
    return graphHashSlowWord(a[0], type, ctx);
}

size_t graphHashSlowWord(Word w, Type* type, GraphHashCtx& ctx) {
    // Scalar and special-repr branches: identical to the fast path.
    if (type && type->repr_ == Type::Repr::DiscriminantEnum) {
        return std::hash<i64>{}(w.i);
    }
    if (type && type->repr_ == Type::Repr::NullablePtrEnum) {
        if (!w.o) return 0;
        auto* et = static_cast<EnumType*>(type);
        int voidIdx = nullablePtrVoidCaseIndex(et);
        int dataIdx = (voidIdx == 0) ? 1 : 0;
        return hashCombine(1, graphHashSlowWord(w, et->cases_[dataIdx].type, ctx));
    }
    if (type && type->repr_ == Type::Repr::UnwrappedTupleStruct) {
        if (auto* st = dynamic_cast<StructType*>(type); st && !st->layout_.empty()) {
            return graphHashSlowWord(w, st->layout_[0].type, ctx);
        }
    }
    if (type == gCurrentVM->intType() || type == gCurrentVM->boolType()) {
        return std::hash<i64>{}(w.i);
    }
    if (type == gCurrentVM->floatType()) {
        return std::hash<f64>{}(w.f);
    }
    if (type == gCurrentVM->symbolType()) {
        return w.s ? w.s->hash() : 0;
    }

    // Leaf object types: no children, no memo.
    if (type == gCurrentVM->stringType()) {
        auto* s = static_cast<StringObj*>(w.o);
        return std::hash<std::string_view>{}(std::string_view(s->s.data(), s->s.size()));
    }
    if (type == gCurrentVM->fractionType()) {
        auto* frac = static_cast<Fraction*>(w.o);
        return hashCombine(std::hash<i64>{}(frac->r.numer()),
                           std::hash<i64>{}(frac->r.denom()));
    }
    if (type == gCurrentVM->complexType()) {
        auto* c = static_cast<Complex*>(w.o);
        return hashCombine(std::hash<f64>{}(c->x.real()), std::hash<f64>{}(c->x.imag()));
    }

    // Heap composites from here down: consult the visited memo. A hit may
    // be a final hash (DAG sharing) or a preliminary one (cycle cut).
    if (w.o) {
        auto it = ctx.memo.find(w.o);
        if (it != ctx.memo.end()) return it->second;
    }

    DepthGuard guard(ctx);

    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* tup = static_cast<Tuple*>(w.o);
        ctx.memo.emplace(w.o, prelim(GCTag::Tuple, tup->numFields_));
        size_t h = tt->fields_.size();
        for (u32 i = 0; i < tup->numFields_; ++i) {
            auto const& f = tt->layout_[i];
            h = hashCombine(h, graphHashSlowWords(&tup->v[f.wordOffset], f.type, ctx));
        }
        ctx.memo[w.o] = h;
        return h;
    }
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* s = static_cast<Struct*>(w.o);
        ctx.memo.emplace(w.o, prelim(GCTag::Struct, std::hash<const void*>{}(st->name_)));
        size_t h = std::hash<const void*>{}(st->name_);
        for (u32 i = 0; i < s->numFields_; ++i) {
            auto const& f = st->layout_[i];
            h = hashCombine(h, graphHashSlowWords(&s->v[f.wordOffset], f.type, ctx));
        }
        ctx.memo[w.o] = h;
        return h;
    }
    if (auto* et = dynamic_cast<EnumType*>(type)) {
        auto* e = static_cast<Enum*>(w.o);
        ctx.memo.emplace(w.o, prelim(GCTag::Enum, (size_t)e->which_));
        size_t h = std::hash<int>{}(e->which_);
        Type* caseType = et->cases_[e->which_].type;
        if (caseType != gCurrentVM->voidType()) {
            h = hashCombine(h, graphHashSlowWords(&e->v[0], caseType, ctx));
        }
        ctx.memo[w.o] = h;
        return h;
    }
    if (dynamic_cast<SetType*>(type)) {
        auto* s = static_cast<SetObj*>(w.o);
        ctx.memo.emplace(w.o, prelim(GCTag::SetObj, s->size()));
        Type* et = s->elemType();
        size_t h = s->size();
        u32 cap = s->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (s->slotState(i) != SetObj::SlotOccupied) continue;
            h ^= graphHashSlowWords(s->slotElem(i), et, ctx);  // XOR -> order-independent
        }
        ctx.memo[w.o] = h;
        return h;
    }
    if (dynamic_cast<MapType*>(type)) {
        auto* m = static_cast<MapObj*>(w.o);
        ctx.memo.emplace(w.o, prelim(GCTag::MapObj, m->size()));
        Type* kt = m->keyType();
        Type* vt = m->valueType();
        size_t h = m->size();
        u32 cap = m->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (m->slotState(i) != MapObj::SlotOccupied) continue;
            h ^= hashCombine(graphHashSlowWords(m->slotKey(i), kt, ctx),
                             graphHashSlowWords(m->slotVal(i), vt, ctx));
        }
        ctx.memo[w.o] = h;
        return h;
    }
    if (auto* arrT = dynamic_cast<ArrayType*>(type)) {
        Type* et = arrT->elemType_;
        switch (arrayBackendFor(et)) {
            case ArrayBackend::Complex: {
                auto* a = static_cast<PodArray<x64>*>(w.o);
                size_t h = a->v.size();
                for (auto const& v : a->v) {
                    h = hashCombine(h, hashCombine(std::hash<f64>{}(v.real()),
                                                   std::hash<f64>{}(v.imag())));
                }
                return h;
            }
            case ArrayBackend::Fraction: {
                auto* a = static_cast<PodArray<r64>*>(w.o);
                size_t h = a->v.size();
                for (auto const& v : a->v) {
                    h = hashCombine(h, hashCombine(std::hash<i64>{}(v.numer()),
                                                   std::hash<i64>{}(v.denom())));
                }
                return h;
            }
            case ArrayBackend::Float: {
                auto* a = static_cast<PodArray<f64>*>(w.o);
                size_t h = a->v.size();
                for (auto val : a->v) h = hashCombine(h, std::hash<f64>{}(val));
                return h;
            }
            case ArrayBackend::Int: {
                auto* a = static_cast<PodArray<i64>*>(w.o);
                size_t h = a->v.size();
                for (auto val : a->v) h = hashCombine(h, std::hash<i64>{}(val));
                return h;
            }
            case ArrayBackend::Inline: {
                auto* a = static_cast<InlineArray*>(w.o);
                ctx.memo.emplace(w.o, prelim(GCTag::InlineArray, a->size()));
                size_t h = a->size();
                for (size_t i = 0; i < a->size(); ++i) {
                    h = hashCombine(h, graphHashSlowWords(a->slot(i), et, ctx));
                }
                ctx.memo[w.o] = h;
                return h;
            }
            case ArrayBackend::Obj: {
                auto* a = static_cast<ObjArray*>(w.o);
                ctx.memo.emplace(w.o, prelim(GCTag::ObjArray, a->size()));
                size_t h = a->size();
                for (auto* obj : *a) {
                    Word ew; ew.o = obj;
                    h = hashCombine(h, graphHashSlowWord(ew, et, ctx));
                }
                ctx.memo[w.o] = h;
                return h;
            }
        }
    }
    if (auto* listT = dynamic_cast<ListType*>(type)) {
        // Forced list nodes are immutable cons cells and cannot form
        // pointer cycles, so no memo: the left-fold combine below must
        // match the fast path exactly. Lazy tails are force-capped.
        Type* et = listT->elemType_;
        auto* node = static_cast<ListNode*>(w.o);
        size_t h = 0;
        while (node) {
            graphForceListNode(node, ctx.forces, "hash");
            h = hashCombine(h, graphHashSlowWords(node->headData(), et, ctx));
            node = node->tail_;
        }
        return h;
    }
    if (dynamic_cast<RangeType*>(type)) {
        auto* r = static_cast<RangeObj*>(w.o);
        auto* rt = static_cast<RangeType*>(r->type_);
        Type* et = rt->elemType_;
        size_t h = std::hash<bool>{}(r->isInfinite_);
        h = hashCombine(h, graphHashSlowWords(r->startData(), et, ctx));
        h = hashCombine(h, graphHashSlowWords(r->stepData(),  et, ctx));
        if (!r->isInfinite_) h = hashCombine(h, graphHashSlowWords(r->endData(), et, ctx));
        return h;
    }
    if (auto* refT = dynamic_cast<RefType*>(type)) {
        if (w.o && w.o->gcTag() == GCTag::InlineRef) {
            auto* ir = static_cast<InlineRef*>(w.o);
            ctx.memo.emplace(w.o, prelim(GCTag::InlineRef, 0));
            size_t h = graphHashSlowWords(&ir->v[0], refT->elemType_, ctx);
            ctx.memo[w.o] = h;
            return h;
        }
        auto* ref = static_cast<RefValue*>(w.o);
        ctx.memo.emplace(w.o, prelim(GCTag::RefValue, 0));
        size_t h = graphHashSlowWord(ref->value_, refT->elemType_, ctx);
        ctx.memo[w.o] = h;
        return h;
    }
    // NOTE: the fast path has no PVec/PMap branches -- persistent
    // collections fall through to the pointer hash -- so the slow path
    // must fall through too (fast/slow agreement outweighs structural
    // hashing here; a structural PVec/PMap hash would have to land in
    // both paths at once).
    // Fallback: hash pointer (same as the fast path).
    return std::hash<void*>{}(w.p);
}

} // namespace ts
