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
//  graph_equal.cpp
//  lang
//
//  Cycle-safe slow-path structural equality (Adams & Dybvig union-find,
//  bisimulation semantics). Mirrors the fast path in value.cpp branch for
//  branch; before descending into a pair of heap composites it consults
//  the equivalence set -- a revisited pair is assumed equal (the
//  coinductive step), a first visit is merged then verified. Any `false`
//  propagates straight to the root, discarding tentative merges, which is
//  what makes union-before-verify sound.
//

#include "value_graph.hpp"
#include "persistent_vector.hpp"
#include "persistent_map.hpp"

#include <stdexcept>

namespace ts {

namespace {

struct DepthGuard {
    GraphEqCtx& ctx;
    explicit DepthGuard(GraphEqCtx& c) : ctx(c) {
        if (++ctx.depth > kGraphMaxDepth) {
            throw std::runtime_error("==: value graph nesting too deep");
        }
    }
    ~DepthGuard() { --ctx.depth; }
};

} // namespace

bool graphEqualSlowWords(Word const* a, Word const* b, Type* type, GraphEqCtx& ctx) {
    if (!type) return a[0].i == b[0].i;
    if (type->repr_ != Type::Repr::Inline) {
        return graphEqualSlowWord(a[0], b[0], type, ctx);
    }
    if (type == gCurrentVM->complexType()) {
        return a[0].f == b[0].f && a[1].f == b[1].f;
    }
    if (type == gCurrentVM->fractionType()) {
        return a[0].i == b[0].i && a[1].i == b[1].i;
    }
    DepthGuard guard(ctx);
    auto cmpFields = [&](auto const& layout) {
        for (auto const& f : layout) {
            if (!f.type) continue;
            if (!graphEqualSlowWords(a + f.wordOffset, b + f.wordOffset, f.type, ctx))
                return false;
        }
        return true;
    };
    if (auto* tt = dynamic_cast<TupleType*>(type))  return cmpFields(tt->layout_);
    if (auto* st = dynamic_cast<StructType*>(type)) return cmpFields(st->layout_);
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        if (a[0].i != b[0].i) return false;
        int which = (int)a[0].i;
        if (which < 0 || (size_t)which >= en->layout_.size()) return true;
        auto const& f = en->layout_[which];
        if (!f.type || f.sizeWords == 0) return true;
        return graphEqualSlowWords(a + f.wordOffset, b + f.wordOffset, f.type, ctx);
    }
    return graphEqualSlowWord(a[0], b[0], type, ctx);
}

bool graphEqualSlowWord(Word a, Word b, Type* type, GraphEqCtx& ctx) {
    // Scalar and special-repr branches: identical to the fast path.
    if (type && type->repr_ == Type::Repr::DiscriminantEnum) {
        return a.i == b.i;
    }
    if (type && type->repr_ == Type::Repr::NullablePtrEnum) {
        if (!a.o || !b.o) return a.o == b.o;
        auto* et = static_cast<EnumType*>(type);
        int voidIdx = nullablePtrVoidCaseIndex(et);
        int dataIdx = (voidIdx == 0) ? 1 : 0;
        return graphEqualSlowWord(a, b, et->cases_[dataIdx].type, ctx);
    }
    if (type && type->repr_ == Type::Repr::UnwrappedTupleStruct) {
        if (auto* st = dynamic_cast<StructType*>(type); st && !st->layout_.empty()) {
            return graphEqualSlowWord(a, b, st->layout_[0].type, ctx);
        }
    }
    if (type == gCurrentVM->intType() || type == gCurrentVM->boolType()) {
        return a.i == b.i;
    }
    if (type == gCurrentVM->floatType()) {
        return a.f == b.f;
    }
    if (type == gCurrentVM->symbolType()) {
        return a.s == b.s;
    }

    // Everything below is a heap object. Identity first (also covers nil);
    // note this makes a pointer-shared subgraph compare equal even if it
    // contains NaN floats -- the price of coinduction, same as Scheme's
    // equal? over eqv?.
    if (!a.o || !b.o) return a.o == b.o;
    if (a.o == b.o) return true;

    // Leaf object types: direct compares, no children, no union-find.
    if (type == gCurrentVM->stringType()) {
        auto* sa = static_cast<StringObj*>(a.o);
        auto* sb = static_cast<StringObj*>(b.o);
        return sa->s == sb->s;
    }
    if (type == gCurrentVM->fractionType()) {
        auto* fa = static_cast<Fraction*>(a.o);
        auto* fb = static_cast<Fraction*>(b.o);
        return fa->r == fb->r;
    }
    if (type == gCurrentVM->complexType()) {
        auto* ca = static_cast<Complex*>(a.o);
        auto* cb = static_cast<Complex*>(b.o);
        return ca->x == cb->x;
    }

    DepthGuard guard(ctx);

    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        if (ctx.uf.unionFind(a.o, b.o)) return true;
        auto* ta = static_cast<Tuple*>(a.o);
        auto* tb = static_cast<Tuple*>(b.o);
        if (ta->numFields_ != tb->numFields_) return false;
        for (u32 i = 0; i < ta->numFields_; ++i) {
            auto const& f = tt->layout_[i];
            if (!graphEqualSlowWords(&ta->v[f.wordOffset], &tb->v[f.wordOffset], f.type, ctx))
                return false;
        }
        return true;
    }
    if (auto* st = dynamic_cast<StructType*>(type)) {
        if (ctx.uf.unionFind(a.o, b.o)) return true;
        auto* sa = static_cast<Struct*>(a.o);
        auto* sb = static_cast<Struct*>(b.o);
        if (sa->numFields_ != sb->numFields_) return false;
        for (u32 i = 0; i < sa->numFields_; ++i) {
            auto const& f = st->layout_[i];
            if (!graphEqualSlowWords(&sa->v[f.wordOffset], &sb->v[f.wordOffset], f.type, ctx))
                return false;
        }
        return true;
    }
    if (auto* et = dynamic_cast<EnumType*>(type)) {
        if (ctx.uf.unionFind(a.o, b.o)) return true;
        auto* ea = static_cast<Enum*>(a.o);
        auto* eb = static_cast<Enum*>(b.o);
        if (ea->which_ != eb->which_) return false;
        Type* caseType = et->cases_[ea->which_].type;
        if (caseType == gCurrentVM->voidType()) return true;
        return graphEqualSlowWords(&ea->v[0], &eb->v[0], caseType, ctx);
    }
    if (dynamic_cast<SetType*>(type)) {
        if (ctx.uf.unionFind(a.o, b.o)) return true;
        auto* sa = static_cast<SetObj*>(a.o);
        auto* sb = static_cast<SetObj*>(b.o);
        if (sa->size() != sb->size()) return false;
        u32 cap = sa->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (sa->slotState(i) != SetObj::SlotOccupied) continue;
            // findSlot's element compares re-enter wordsEqual, which joins
            // this traversal through gGraphEqCtx.
            if (sb->findSlot(sa->slotElem(i)) == sb->capacity()) return false;
        }
        return true;
    }
    if (dynamic_cast<MapType*>(type)) {
        if (ctx.uf.unionFind(a.o, b.o)) return true;
        auto* ma = static_cast<MapObj*>(a.o);
        auto* mb = static_cast<MapObj*>(b.o);
        if (ma->size() != mb->size()) return false;
        Type* vt = ma->valueType();
        u32 cap = ma->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (ma->slotState(i) != MapObj::SlotOccupied) continue;
            u32 bs = mb->findSlot(ma->slotKey(i));
            if (bs == mb->capacity()) return false;
            if (!graphEqualSlowWords(ma->slotVal(i), mb->slotVal(bs), vt, ctx)) return false;
        }
        return true;
    }
    if (auto* arrT = dynamic_cast<ArrayType*>(type)) {
        if (ctx.uf.unionFind(a.o, b.o)) return true;
        Type* et = arrT->elemType_;
        switch (arrayBackendFor(et)) {
            case ArrayBackend::Complex: {
                auto* aa = static_cast<PodArray<x64>*>(a.o);
                auto* ab = static_cast<PodArray<x64>*>(b.o);
                return aa->v == ab->v;
            }
            case ArrayBackend::Fraction: {
                auto* aa = static_cast<PodArray<r64>*>(a.o);
                auto* ab = static_cast<PodArray<r64>*>(b.o);
                if (aa->v.size() != ab->v.size()) return false;
                for (size_t i = 0; i < aa->v.size(); ++i) {
                    if (aa->v[i].numer() != ab->v[i].numer()) return false;
                    if (aa->v[i].denom() != ab->v[i].denom()) return false;
                }
                return true;
            }
            case ArrayBackend::Float: {
                auto* aa = static_cast<PodArray<f64>*>(a.o);
                auto* ab = static_cast<PodArray<f64>*>(b.o);
                return aa->v == ab->v;
            }
            case ArrayBackend::Int: {
                auto* aa = static_cast<PodArray<i64>*>(a.o);
                auto* ab = static_cast<PodArray<i64>*>(b.o);
                return aa->v == ab->v;
            }
            case ArrayBackend::Inline: {
                auto* aa = static_cast<InlineArray*>(a.o);
                auto* ab = static_cast<InlineArray*>(b.o);
                if (aa->size() != ab->size()) return false;
                for (size_t i = 0; i < aa->size(); ++i) {
                    if (!graphEqualSlowWords(aa->slot(i), ab->slot(i), et, ctx)) return false;
                }
                return true;
            }
            case ArrayBackend::Obj: {
                auto* aa = static_cast<ObjArray*>(a.o);
                auto* ab = static_cast<ObjArray*>(b.o);
                if (aa->size() != ab->size()) return false;
                for (size_t i = 0; i < aa->size(); ++i) {
                    Word wa; wa.o = aa->get(i);
                    Word wb; wb.o = ab->get(i);
                    if (!graphEqualSlowWord(wa, wb, et, ctx)) return false;
                }
                return true;
            }
        }
    }
    if (auto* listT = dynamic_cast<ListType*>(type)) {
        Type* et = listT->elemType_;
        auto* na = static_cast<ListNode*>(a.o);
        auto* nb = static_cast<ListNode*>(b.o);
        while (na && nb) {
            if (na == nb) return true;
            if (ctx.uf.unionFind(na, nb)) return true;
            graphForceListNode(na, ctx.forces, "==");
            graphForceListNode(nb, ctx.forces, "==");
            if (!graphEqualSlowWords(na->headData(), nb->headData(), et, ctx)) return false;
            na = na->tail_;
            nb = nb->tail_;
        }
        return na == nb;  // both must be null
    }
    if (auto* rt = dynamic_cast<RangeType*>(type)) {
        auto* ra = static_cast<RangeObj*>(a.o);
        auto* rb = static_cast<RangeObj*>(b.o);
        if (ra->isInfinite_ != rb->isInfinite_) return false;
        Type* et = rt->elemType_;
        if (!graphEqualSlowWords(ra->startData(), rb->startData(), et, ctx)) return false;
        if (!graphEqualSlowWords(ra->stepData(),  rb->stepData(),  et, ctx)) return false;
        if (!ra->isInfinite_ && !graphEqualSlowWords(ra->endData(), rb->endData(), et, ctx)) return false;
        return true;
    }
    if (auto* refT = dynamic_cast<RefType*>(type)) {
        if (ctx.uf.unionFind(a.o, b.o)) return true;
        if (a.o->gcTag() == GCTag::InlineRef) {
            auto* ia = static_cast<InlineRef*>(a.o);
            auto* ib = static_cast<InlineRef*>(b.o);
            return graphEqualSlowWords(&ia->v[0], &ib->v[0], refT->elemType_, ctx);
        }
        auto* ra = static_cast<RefValue*>(a.o);
        auto* rb = static_cast<RefValue*>(b.o);
        return graphEqualSlowWord(ra->value_, rb->value_, refT->elemType_, ctx);
    }
    if (auto* pvT = dynamic_cast<PersistentVectorType*>(type)) {
        if (ctx.uf.unionFind(a.o, b.o)) return true;
        auto* va = static_cast<PVec*>(a.o);
        auto* vb = static_cast<PVec*>(b.o);
        if (va->count_ != vb->count_) return false;
        Type* et = pvT->elemType_;
        for (u32 i = 0; i < va->count_; ++i) {
            if (!graphEqualSlowWords(va->elemAt(i), vb->elemAt(i), et, ctx)) return false;
        }
        return true;
    }
    if (auto* pmT = dynamic_cast<PersistentMapType*>(type)) {
        if (ctx.uf.unionFind(a.o, b.o)) return true;
        auto* ma = static_cast<PMap*>(a.o);
        auto* mb = static_cast<PMap*>(b.o);
        if (ma->count_ != mb->count_) return false;
        Type* vt = pmT->valueType_;
        PMapIter it(ma);
        u32 kS = strideForType(pmT->keyType_);
        while (Word const* pair = it.next()) {
            Word const* bv = mb->get(pair);
            if (!bv) return false;
            if (!graphEqualSlowWords(pair + kS, bv, vt, ctx)) return false;
        }
        return true;
    }
    return a.p == b.p;
}

} // namespace ts
