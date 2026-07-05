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
//  value_serialize.cpp
//  lang
//
//  TZV1 encoder/decoder. See value_serialize.hpp for the wire layout.
//
//  Encoder: pass 1 walks the graph iteratively (worklist, no C++ recursion
//  over the object graph) assigning ids in canonical discovery order;
//  pass 2 emits headers + contents. Map/Set/PMap children are visited in
//  an order sorted by a self-contained canonical key encoding, so the
//  output bytes do not depend on hash-table layout or insertion history.
//
//  Decoder: validates the signature against the expected type, allocates
//  all object shells (types known from the signature's type table), fills
//  contents resolving ids to pointers, then populates hash containers
//  last, in decreasing id order, so keys hash over final data.
//

#include "value_serialize.hpp"
#include "value_graph.hpp"
#include "persistent_vector.hpp"
#include "persistent_map.hpp"
#include "symbol.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace ts {

namespace {

[[noreturn]] void fail(char const* what) {
    throw std::runtime_error(std::string("deserialize: ") + what);
}
[[noreturn]] void failEnc(char const* what) {
    throw std::runtime_error(std::string("serialize: ") + what);
}

// --- primitives ---

void putVarint(Vec<u8>& out, u64 v) {
    while (v >= 0x80) {
        out.push_back((u8)(v | 0x80));
        v >>= 7;
    }
    out.push_back((u8)v);
}

void putU64(Vec<u8>& out, u64 v) {
    for (int i = 0; i < 8; ++i) out.push_back((u8)(v >> (8 * i)));
}

void putBytes(Vec<u8>& out, void const* p, size_t n) {
    u8 const* b = (u8 const*)p;
    out.insert(out.end(), b, b + n);
}

void putSymbolText(Vec<u8>& out, SymbolPtr s) {
    std::string_view sv = s ? s->str() : std::string_view{};
    putVarint(out, sv.size());
    putBytes(out, sv.data(), sv.size());
}

struct Reader {
    u8 const* p;
    u8 const* end;

    void need(size_t n) const {
        if ((size_t)(end - p) < n) fail("truncated buffer");
    }
    u8 u8v() { need(1); return *p++; }
    u64 varint() {
        u64 v = 0;
        int shift = 0;
        while (true) {
            need(1);
            u8 b = *p++;
            v |= (u64)(b & 0x7f) << shift;
            if (!(b & 0x80)) return v;
            shift += 7;
            if (shift > 63) fail("varint overflow");
        }
    }
    u64 u64le() {
        need(8);
        u64 v = 0;
        for (int i = 0; i < 8; ++i) v |= (u64)p[i] << (8 * i);
        p += 8;
        return v;
    }
    u8 const* raw(size_t n) { need(n); u8 const* r = p; p += n; return r; }
};

// --- type signature kinds ---

enum : u8 {
    TS_BOOL = 1, TS_INT, TS_FLOAT, TS_SYMBOL, TS_STRING, TS_BYTES,
    TS_COMPLEX, TS_FRACTION, TS_RANGE, TS_ARRAY, TS_LIST, TS_MAP, TS_SET,
    TS_REF, TS_PVEC, TS_PMAP, TS_TUPLE, TS_STRUCT, TS_ENUM, TS_VOID,
    TS_BACKREF,
};

// --- object serial kinds ---

enum : u8 {
    SK_STRING = 1, SK_BYTES, SK_FRACTION, SK_COMPLEX, SK_POD_ARRAY,
    SK_OBJ_ARRAY, SK_INLINE_ARRAY, SK_MAP, SK_SET, SK_LIST_NODE, SK_REF,
    SK_INLINE_REF, SK_STRUCT, SK_TUPLE, SK_ENUM, SK_RANGE, SK_PVEC, SK_PMAP,
};

BuiltinTypes const& bt() { return gCurrentTypeUniverse->types(); }

template <typename K, typename V>
Map<K, V> makeMap() {
    return Map<K, V>(0, std::hash<K>{}, std::equal_to<K>{},
                     rt::STLAllocator<std::pair<K const, V>>(rt::gCurrentAllocator));
}
template <typename T>
Vec<T> makeVec() {
    return Vec<T>{rt::STLAllocator<T>(rt::gCurrentAllocator)};
}

struct SigBuilder {
    Vec<u8>& out;
    Vec<Type*>* order;
    Map<Type*, u32> seen = makeMap<Type*, u32>();  // type -> ordinal in `order` walk

    u32 nextOrdinal = 0;

    // Registers t; returns false (and emits a backref) when already seen.
    bool enter(Type* t) {
        auto it = seen.find(t);
        if (it != seen.end()) {
            out.push_back(TS_BACKREF);
            putVarint(out, it->second);
            return false;
        }
        seen.emplace(t, nextOrdinal++);
        if (order) order->push_back(t);
        return true;
    }

    void walk(Type* t) {
        if (!t) failEnc("null type in signature");
        if (!enter(t)) return;
        auto const& b = bt();
        if (t == b.boolType)     { out.push_back(TS_BOOL); return; }
        if (t == b.intType)      { out.push_back(TS_INT); return; }
        if (t == b.floatType)    { out.push_back(TS_FLOAT); return; }
        if (t == b.symbolType)   { out.push_back(TS_SYMBOL); return; }
        if (t == b.stringType)   { out.push_back(TS_STRING); return; }
        if (t == b.bytesType)    { out.push_back(TS_BYTES); return; }
        if (t == b.complexType)  { out.push_back(TS_COMPLEX); return; }
        if (t == b.fractionType) { out.push_back(TS_FRACTION); return; }
        if (auto* rt2 = dynamic_cast<RangeType*>(t)) { out.push_back(TS_RANGE); walk(rt2->elemType_); return; }
        if (auto* at = dynamic_cast<ArrayType*>(t))  { out.push_back(TS_ARRAY); walk(at->elemType_); return; }
        if (auto* lt = dynamic_cast<ListType*>(t))   { out.push_back(TS_LIST);  walk(lt->elemType_); return; }
        if (auto* mt = dynamic_cast<MapType*>(t))    { out.push_back(TS_MAP);   walk(mt->keyType_); walk(mt->valueType_); return; }
        if (auto* st2 = dynamic_cast<SetType*>(t))   { out.push_back(TS_SET);   walk(st2->elemType_); return; }
        if (auto* rf = dynamic_cast<RefType*>(t))    { out.push_back(TS_REF);   walk(rf->elemType_); return; }
        if (auto* pv = dynamic_cast<PersistentVectorType*>(t)) { out.push_back(TS_PVEC); walk(pv->elemType_); return; }
        if (auto* pm = dynamic_cast<PersistentMapType*>(t))    { out.push_back(TS_PMAP); walk(pm->keyType_); walk(pm->valueType_); return; }
        if (auto* tt = dynamic_cast<TupleType*>(t)) {
            out.push_back(TS_TUPLE);
            putVarint(out, tt->fields_.size());
            for (Type* ft : tt->fields_) walk(ft);
            return;
        }
        if (auto* st = dynamic_cast<StructType*>(t)) {
            out.push_back(TS_STRUCT);
            putSymbolText(out, st->name_);
            out.push_back(st->isTupleStruct_ ? 1 : 0);
            putVarint(out, st->fields_.size());
            for (auto const& f : st->fields_) {
                putSymbolText(out, f.name);
                walk(f.type);
            }
            return;
        }
        if (auto* en = dynamic_cast<EnumType*>(t)) {
            out.push_back(TS_ENUM);
            putSymbolText(out, en->name_);
            putVarint(out, en->cases_.size());
            for (auto const& c : en->cases_) {
                putSymbolText(out, c.name);
                if (!c.type || c.type == bt().voidType) out.push_back(TS_VOID);
                else walk(c.type);
            }
            return;
        }
        failEnc("type is not serializable");
    }
};

// --- canonical (self-contained) value encoding, for Map/Set entry order ---
//
// Structural bytes with local first-visit numbering for heap children, so
// the encoding is a pure function of the value: no ids, no pointers, no
// hash-table order (nested map/set entries are themselves sorted).

struct CanonCtx {
    Map<Obj*, u32> seen = makeMap<Obj*, u32>();
    u32 depth = 0;
    i64* forces;
};

struct CanonDepth {
    CanonCtx& c;
    explicit CanonDepth(CanonCtx& c_) : c(c_) {
        if (++c.depth > kGraphMaxDepth) failEnc("value graph nesting too deep");
    }
    ~CanonDepth() { --c.depth; }
};

void canonValue(Word const* base, Type* t, Vec<u8>& out, CanonCtx& c);

void canonObject(Obj* o, Type* t, Vec<u8>& out, CanonCtx& c) {
    auto const& b = bt();
    CanonDepth guard(c);
    if (t == b.stringType) {
        auto* s = static_cast<StringObj*>(o);
        putVarint(out, s->s.size());
        putBytes(out, s->s.data(), s->s.size());
        return;
    }
    if (t == b.bytesType) {
        auto* s = static_cast<BytesObj*>(o);
        putVarint(out, s->data.size());
        putBytes(out, s->data.data(), s->data.size());
        return;
    }
    if (t == b.fractionType) {
        auto* f = static_cast<Fraction*>(o);
        putU64(out, (u64)f->r.numer());
        putU64(out, (u64)f->r.denom());
        return;
    }
    if (t == b.complexType) {
        auto* x = static_cast<Complex*>(o);
        Word w1; w1.f = x->x.real(); putU64(out, (u64)w1.i);
        Word w2; w2.f = x->x.imag(); putU64(out, (u64)w2.i);
        return;
    }
    if (auto* at = dynamic_cast<ArrayType*>(t)) {
        Type* et = at->elemType_;
        switch (arrayBackendFor(et)) {
            case ArrayBackend::Int: {
                auto* a = static_cast<PodArray<i64>*>(o);
                putVarint(out, a->v.size());
                for (auto v : a->v) putU64(out, (u64)v);
                return;
            }
            case ArrayBackend::Float: {
                auto* a = static_cast<PodArray<f64>*>(o);
                putVarint(out, a->v.size());
                for (auto v : a->v) { Word w; w.f = v; putU64(out, (u64)w.i); }
                return;
            }
            case ArrayBackend::Complex: {
                auto* a = static_cast<PodArray<x64>*>(o);
                putVarint(out, a->v.size());
                for (auto const& v : a->v) {
                    Word w1; w1.f = v.real(); putU64(out, (u64)w1.i);
                    Word w2; w2.f = v.imag(); putU64(out, (u64)w2.i);
                }
                return;
            }
            case ArrayBackend::Fraction: {
                auto* a = static_cast<PodArray<r64>*>(o);
                putVarint(out, a->v.size());
                for (auto const& v : a->v) {
                    putU64(out, (u64)v.numer());
                    putU64(out, (u64)v.denom());
                }
                return;
            }
            case ArrayBackend::Inline: {
                auto* a = static_cast<InlineArray*>(o);
                putVarint(out, a->size());
                for (size_t i = 0; i < a->size(); ++i) canonValue(a->slot(i), et, out, c);
                return;
            }
            case ArrayBackend::Obj: {
                auto* a = static_cast<ObjArray*>(o);
                putVarint(out, a->size());
                for (size_t i = 0; i < a->size(); ++i) {
                    Word w; w.o = a->get(i);
                    canonValue(&w, et, out, c);
                }
                return;
            }
        }
    }
    if (auto* mt = dynamic_cast<MapType*>(t)) {
        auto* m = static_cast<MapObj*>(o);
        putVarint(out, m->size());
        // Sort entries by their own canonical encodings.
        auto entries = makeVec<VMString>();
        u32 cap = m->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (m->slotState(i) != MapObj::SlotOccupied) continue;
            auto entry = makeVec<u8>();
            canonValue(m->slotKey(i), m->keyType(), entry, c);
            canonValue(m->slotVal(i), m->valueType(), entry, c);
            entries.push_back(VMString((char const*)entry.data(), entry.size(),
                                       rt::STLAllocator<char>(rt::gCurrentAllocator)));
        }
        std::sort(entries.begin(), entries.end());
        for (auto const& e : entries) putBytes(out, e.data(), e.size());
        return;
    }
    if (dynamic_cast<SetType*>(t)) {
        auto* s = static_cast<SetObj*>(o);
        putVarint(out, s->size());
        auto elems = makeVec<VMString>();
        u32 cap = s->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (s->slotState(i) != SetObj::SlotOccupied) continue;
            auto e = makeVec<u8>();
            canonValue(s->slotElem(i), s->elemType(), e, c);
            elems.push_back(VMString((char const*)e.data(), e.size(),
                                     rt::STLAllocator<char>(rt::gCurrentAllocator)));
        }
        std::sort(elems.begin(), elems.end());
        for (auto const& e : elems) putBytes(out, e.data(), e.size());
        return;
    }
    if (auto* lt = dynamic_cast<ListType*>(t)) {
        Type* et = lt->elemType_;
        auto* node = static_cast<ListNode*>(o);
        while (node) {
            graphForceListNode(node, *c.forces, "serialize");
            out.push_back(1);
            canonValue(node->headData(), et, out, c);
            ListNode* tail = node->tail_;
            if (!tail) break;
            // Tail sharing/cycles: number nodes locally like other objects.
            auto it = c.seen.find(tail);
            if (it != c.seen.end()) {
                out.push_back(2);
                putVarint(out, it->second);
                return;
            }
            c.seen.emplace(tail, (u32)c.seen.size());
            node = tail;
        }
        out.push_back(0);
        return;
    }
    if (auto* rt2 = dynamic_cast<RangeType*>(t)) {
        auto* r = static_cast<RangeObj*>(o);
        Type* et = rt2->elemType_;
        out.push_back(r->isInfinite_ ? 1 : 0);
        canonValue(r->startData(), et, out, c);
        canonValue(r->stepData(), et, out, c);
        if (!r->isInfinite_) canonValue(r->endData(), et, out, c);
        return;
    }
    if (auto* rf = dynamic_cast<RefType*>(t)) {
        if (o->gcTag() == GCTag::InlineRef) {
            auto* ir = static_cast<InlineRef*>(o);
            canonValue(&ir->v[0], rf->elemType_, out, c);
        } else {
            auto* r = static_cast<RefValue*>(o);
            canonValue(&r->value_, rf->elemType_, out, c);
        }
        return;
    }
    if (auto* tt = dynamic_cast<TupleType*>(t)) {
        auto* tup = static_cast<Tuple*>(o);
        for (u32 i = 0; i < tup->numFields_; ++i) {
            auto const& f = tt->layout_[i];
            canonValue(&tup->v[f.wordOffset], f.type, out, c);
        }
        return;
    }
    if (auto* st = dynamic_cast<StructType*>(t)) {
        auto* s = static_cast<Struct*>(o);
        for (u32 i = 0; i < s->numFields_; ++i) {
            auto const& f = st->layout_[i];
            canonValue(&s->v[f.wordOffset], f.type, out, c);
        }
        return;
    }
    if (auto* en = dynamic_cast<EnumType*>(t)) {
        auto* e = static_cast<Enum*>(o);
        putVarint(out, (u64)e->which_);
        Type* ct = en->cases_[e->which_].type;
        if (ct && ct != bt().voidType) canonValue(&e->v[0], ct, out, c);
        return;
    }
    if (auto* pv = dynamic_cast<PersistentVectorType*>(t)) {
        auto* v = static_cast<PVec*>(o);
        putVarint(out, v->count_);
        for (u32 i = 0; i < v->count_; ++i) canonValue(v->elemAt(i), pv->elemType_, out, c);
        return;
    }
    if (auto* pm = dynamic_cast<PersistentMapType*>(t)) {
        auto* m = static_cast<PMap*>(o);
        putVarint(out, m->count_);
        u32 kS = strideForType(pm->keyType_);
        auto entries = makeVec<VMString>();
        PMapIter it(m);
        while (Word const* pair = it.next()) {
            auto e = makeVec<u8>();
            canonValue(pair, pm->keyType_, e, c);
            canonValue(pair + kS, pm->valueType_, e, c);
            entries.push_back(VMString((char const*)e.data(), e.size(),
                                       rt::STLAllocator<char>(rt::gCurrentAllocator)));
        }
        std::sort(entries.begin(), entries.end());
        for (auto const& e : entries) putBytes(out, e.data(), e.size());
        return;
    }
    failEnc("value is not serializable");
}

void canonValue(Word const* base, Type* t, Vec<u8>& out, CanonCtx& c) {
    if (!t) failEnc("null type");
    if (t->repr_ == Type::Repr::Inline) {
        auto const& b = bt();
        if (t == b.complexType || t == b.fractionType) {
            putU64(out, (u64)base[0].i);
            putU64(out, (u64)base[1].i);
            return;
        }
        CanonDepth guard(c);
        if (auto* tt = dynamic_cast<TupleType*>(t)) {
            for (size_t i = 0; i < tt->fields_.size(); ++i) {
                auto const& f = tt->layout_[i];
                canonValue(base + f.wordOffset, f.type, out, c);
            }
            return;
        }
        if (auto* st = dynamic_cast<StructType*>(t)) {
            for (size_t i = 0; i < st->fields_.size(); ++i) {
                auto const& f = st->layout_[i];
                canonValue(base + f.wordOffset, f.type, out, c);
            }
            return;
        }
        if (auto* en = dynamic_cast<EnumType*>(t)) {
            int which = (int)base[0].i;
            putVarint(out, (u64)which);
            if (which >= 0 && (size_t)which < en->layout_.size()) {
                auto const& f = en->layout_[which];
                if (f.type && f.sizeWords > 0 && f.type != bt().voidType) {
                    canonValue(base + f.wordOffset, f.type, out, c);
                }
            }
            return;
        }
        failEnc("value is not serializable");
    }
    // 1-word slot
    Word w = base[0];
    if (t->repr_ == Type::Repr::DiscriminantEnum) { putU64(out, (u64)w.i); return; }
    if (t->repr_ == Type::Repr::UnwrappedTupleStruct) {
        auto* st = static_cast<StructType*>(t);
        canonValue(&w, st->layout_[0].type, out, c);
        return;
    }
    if (t->repr_ == Type::Repr::NullablePtrEnum) {
        if (!w.o) { out.push_back(0); return; }
        out.push_back(1);
        auto* et = static_cast<EnumType*>(t);
        int voidIdx = nullablePtrVoidCaseIndex(et);
        int dataIdx = (voidIdx == 0) ? 1 : 0;
        canonValue(&w, et->cases_[dataIdx].type, out, c);
        return;
    }
    auto const& b = bt();
    if (t == b.intType || t == b.boolType || t == b.floatType) {
        putU64(out, (u64)w.i);
        return;
    }
    if (t == b.symbolType) { putSymbolText(out, w.s); return; }
    if (!t->isObjType()) failEnc("value is not serializable");
    if (!w.o) { out.push_back(0); return; }
    auto it = c.seen.find(w.o);
    if (it != c.seen.end()) {
        out.push_back(2);
        putVarint(out, it->second);
        return;
    }
    out.push_back(1);
    c.seen.emplace(w.o, (u32)c.seen.size());
    canonObject(w.o, t, out, c);
}

// --- encoder ---

struct Encoder {
    Map<Obj*, u32> ids = makeMap<Obj*, u32>();        // Obj* -> id (1-based)
    Vec<Obj*> objs = makeVec<Obj*>();                 // id-1 -> obj
    Vec<Type*> objTypes = makeVec<Type*>();           // id-1 -> slot type
    Map<Obj*, Vec<u32>> canonOrder = makeMap<Obj*, Vec<u32>>();  // Map/Set slot order; PMap seq order
    Map<Type*, u32> typeIndex = makeMap<Type*, u32>();
    i64 forces = 0;

    u32 idOf(Obj* o) const {
        auto it = ids.find(o);
        if (it == ids.end()) failEnc("value mutated during serialization");
        return it->second;
    }

    u32 addObj(Obj* o, Type* slotType) {
        auto it = ids.find(o);
        if (it != ids.end()) return it->second;
        u32 id = (u32)objs.size() + 1;
        ids.emplace(o, id);
        objs.push_back(o);
        objTypes.push_back(slotType);
        return id;
    }
};

// Register heap children of one value slot (recursing through inline
// composites only -- type-bounded, no graph recursion).
void discoverValue(Encoder& e, Word const* base, Type* t) {
    if (!t) failEnc("null type");
    if (t->repr_ == Type::Repr::Inline) {
        auto const& b = bt();
        if (t == b.complexType || t == b.fractionType) return;
        if (auto* tt = dynamic_cast<TupleType*>(t)) {
            for (size_t i = 0; i < tt->fields_.size(); ++i) {
                auto const& f = tt->layout_[i];
                discoverValue(e, base + f.wordOffset, f.type);
            }
            return;
        }
        if (auto* st = dynamic_cast<StructType*>(t)) {
            for (size_t i = 0; i < st->fields_.size(); ++i) {
                auto const& f = st->layout_[i];
                discoverValue(e, base + f.wordOffset, f.type);
            }
            return;
        }
        if (auto* en = dynamic_cast<EnumType*>(t)) {
            int which = (int)base[0].i;
            if (which >= 0 && (size_t)which < en->layout_.size()) {
                auto const& f = en->layout_[which];
                if (f.type && f.sizeWords > 0 && f.type != bt().voidType) {
                    discoverValue(e, base + f.wordOffset, f.type);
                }
            }
            return;
        }
        failEnc("value is not serializable");
    }
    Word w = base[0];
    if (t->repr_ == Type::Repr::DiscriminantEnum) return;
    if (t->repr_ == Type::Repr::UnwrappedTupleStruct) {
        auto* st = static_cast<StructType*>(t);
        discoverValue(e, &w, st->layout_[0].type);
        return;
    }
    if (t->repr_ == Type::Repr::NullablePtrEnum) {
        if (!w.o) return;
        auto* et = static_cast<EnumType*>(t);
        int voidIdx = nullablePtrVoidCaseIndex(et);
        int dataIdx = (voidIdx == 0) ? 1 : 0;
        discoverValue(e, &w, et->cases_[dataIdx].type);
        return;
    }
    auto const& b = bt();
    if (t == b.intType || t == b.boolType || t == b.floatType || t == b.symbolType) return;
    if (!t->isObjType()) failEnc("value is not serializable");
    if (w.o) e.addObj(w.o, t);
}

// Register the children of an already-discovered object, computing (and
// caching) the canonical entry order for hash containers.
void discoverObject(Encoder& e, Obj* o, Type* t) {
    auto const& b = bt();
    if (t == b.stringType || t == b.bytesType || t == b.fractionType || t == b.complexType) return;
    if (auto* at = dynamic_cast<ArrayType*>(t)) {
        Type* et = at->elemType_;
        switch (arrayBackendFor(et)) {
            case ArrayBackend::Inline: {
                auto* a = static_cast<InlineArray*>(o);
                for (size_t i = 0; i < a->size(); ++i) discoverValue(e, a->slot(i), et);
                return;
            }
            case ArrayBackend::Obj: {
                auto* a = static_cast<ObjArray*>(o);
                for (size_t i = 0; i < a->size(); ++i) {
                    Word w; w.o = a->get(i);
                    discoverValue(e, &w, et);
                }
                return;
            }
            default: return;   // POD backends: no heap children
        }
    }
    if (auto* mt = dynamic_cast<MapType*>(t)) {
        auto* m = static_cast<MapObj*>(o);
        auto order = makeVec<u32>();
        auto keys = makeVec<VMString>();
        u32 cap = m->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (m->slotState(i) != MapObj::SlotOccupied) continue;
            CanonCtx c; c.forces = &e.forces;
            auto kb = makeVec<u8>();
            canonValue(m->slotKey(i), m->keyType(), kb, c);
            order.push_back(i);
            keys.push_back(VMString((char const*)kb.data(), kb.size(),
                                    rt::STLAllocator<char>(rt::gCurrentAllocator)));
        }
        auto perm = makeVec<u32>();
        for (u32 i = 0; i < (u32)order.size(); ++i) perm.push_back(i);
        std::sort(perm.begin(), perm.end(), [&](u32 x, u32 y) { return keys[x] < keys[y]; });
        auto sorted = makeVec<u32>();
        for (u32 pi : perm) sorted.push_back(order[pi]);
        for (u32 slot : sorted) {
            discoverValue(e, m->slotKey(slot), m->keyType());
            discoverValue(e, m->slotVal(slot), m->valueType());
        }
        e.canonOrder.emplace(o, std::move(sorted));
        return;
    }
    if (dynamic_cast<SetType*>(t)) {
        auto* s = static_cast<SetObj*>(o);
        auto order = makeVec<u32>();
        auto keys = makeVec<VMString>();
        u32 cap = s->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (s->slotState(i) != SetObj::SlotOccupied) continue;
            CanonCtx c; c.forces = &e.forces;
            auto kb = makeVec<u8>();
            canonValue(s->slotElem(i), s->elemType(), kb, c);
            order.push_back(i);
            keys.push_back(VMString((char const*)kb.data(), kb.size(),
                                    rt::STLAllocator<char>(rt::gCurrentAllocator)));
        }
        auto perm = makeVec<u32>();
        for (u32 i = 0; i < (u32)order.size(); ++i) perm.push_back(i);
        std::sort(perm.begin(), perm.end(), [&](u32 x, u32 y) { return keys[x] < keys[y]; });
        auto sorted = makeVec<u32>();
        for (u32 pi : perm) sorted.push_back(order[pi]);
        for (u32 slot : sorted) discoverValue(e, s->slotElem(slot), s->elemType());
        e.canonOrder.emplace(o, std::move(sorted));
        return;
    }
    if (auto* lt = dynamic_cast<ListType*>(t)) {
        auto* node = static_cast<ListNode*>(o);
        graphForceListNode(node, e.forces, "serialize");
        discoverValue(e, node->headData(), lt->elemType_);
        if (node->tail_) e.addObj(node->tail_, t);
        return;
    }
    if (dynamic_cast<RangeType*>(t)) return;   // Int/Fraction endpoints: no heap children
    if (auto* rf = dynamic_cast<RefType*>(t)) {
        if (o->gcTag() == GCTag::InlineRef) {
            auto* ir = static_cast<InlineRef*>(o);
            discoverValue(e, &ir->v[0], rf->elemType_);
        } else {
            auto* r = static_cast<RefValue*>(o);
            discoverValue(e, &r->value_, rf->elemType_);
        }
        return;
    }
    if (auto* tt = dynamic_cast<TupleType*>(t)) {
        auto* tup = static_cast<Tuple*>(o);
        for (u32 i = 0; i < tup->numFields_; ++i) {
            auto const& f = tt->layout_[i];
            discoverValue(e, &tup->v[f.wordOffset], f.type);
        }
        return;
    }
    if (auto* st = dynamic_cast<StructType*>(t)) {
        auto* s = static_cast<Struct*>(o);
        for (u32 i = 0; i < s->numFields_; ++i) {
            auto const& f = st->layout_[i];
            discoverValue(e, &s->v[f.wordOffset], f.type);
        }
        return;
    }
    if (auto* en = dynamic_cast<EnumType*>(t)) {
        auto* ev = static_cast<Enum*>(o);
        Type* ct = en->cases_[ev->which_].type;
        if (ct && ct != bt().voidType) discoverValue(e, &ev->v[0], ct);
        return;
    }
    if (auto* pv = dynamic_cast<PersistentVectorType*>(t)) {
        auto* v = static_cast<PVec*>(o);
        for (u32 i = 0; i < v->count_; ++i) discoverValue(e, v->elemAt(i), pv->elemType_);
        return;
    }
    if (auto* pm = dynamic_cast<PersistentMapType*>(t)) {
        auto* m = static_cast<PMap*>(o);
        u32 kS = strideForType(pm->keyType_);
        // Collect pairs in iteration order, then canonical-sort.
        auto pairs = makeVec<Word const*>();
        PMapIter it(m);
        while (Word const* pair = it.next()) pairs.push_back(pair);
        auto keys = makeVec<VMString>();
        for (auto* pair : pairs) {
            CanonCtx c; c.forces = &e.forces;
            auto kb = makeVec<u8>();
            canonValue(pair, pm->keyType_, kb, c);
            keys.push_back(VMString((char const*)kb.data(), kb.size(),
                                    rt::STLAllocator<char>(rt::gCurrentAllocator)));
        }
        auto perm = makeVec<u32>();
        for (u32 i = 0; i < (u32)pairs.size(); ++i) perm.push_back(i);
        std::sort(perm.begin(), perm.end(), [&](u32 x, u32 y) { return keys[x] < keys[y]; });
        for (u32 pi : perm) {
            discoverValue(e, pairs[pi], pm->keyType_);
            discoverValue(e, pairs[pi] + kS, pm->valueType_);
        }
        e.canonOrder.emplace(o, std::move(perm));
        return;
    }
    failEnc("value is not serializable");
}

// Emit one value slot: atoms inline, heap references as ids.
void encodeValue(Encoder& e, Word const* base, Type* t, Vec<u8>& out) {
    if (t->repr_ == Type::Repr::Inline) {
        auto const& b = bt();
        if (t == b.complexType || t == b.fractionType) {
            putU64(out, (u64)base[0].i);
            putU64(out, (u64)base[1].i);
            return;
        }
        if (auto* tt = dynamic_cast<TupleType*>(t)) {
            for (size_t i = 0; i < tt->fields_.size(); ++i) {
                auto const& f = tt->layout_[i];
                encodeValue(e, base + f.wordOffset, f.type, out);
            }
            return;
        }
        if (auto* st = dynamic_cast<StructType*>(t)) {
            for (size_t i = 0; i < st->fields_.size(); ++i) {
                auto const& f = st->layout_[i];
                encodeValue(e, base + f.wordOffset, f.type, out);
            }
            return;
        }
        if (auto* en = dynamic_cast<EnumType*>(t)) {
            int which = (int)base[0].i;
            putVarint(out, (u64)which);
            if (which >= 0 && (size_t)which < en->layout_.size()) {
                auto const& f = en->layout_[which];
                if (f.type && f.sizeWords > 0 && f.type != bt().voidType) {
                    encodeValue(e, base + f.wordOffset, f.type, out);
                }
            }
            return;
        }
        failEnc("value is not serializable");
    }
    Word w = base[0];
    if (t->repr_ == Type::Repr::DiscriminantEnum) { putU64(out, (u64)w.i); return; }
    if (t->repr_ == Type::Repr::UnwrappedTupleStruct) {
        auto* st = static_cast<StructType*>(t);
        encodeValue(e, &w, st->layout_[0].type, out);
        return;
    }
    if (t->repr_ == Type::Repr::NullablePtrEnum) {
        putVarint(out, w.o ? e.idOf(w.o) : 0);
        return;
    }
    auto const& b = bt();
    if (t == b.intType || t == b.boolType || t == b.floatType) {
        putU64(out, (u64)w.i);
        return;
    }
    if (t == b.symbolType) { putSymbolText(out, w.s); return; }
    if (!t->isObjType()) failEnc("value is not serializable");
    putVarint(out, w.o ? e.idOf(w.o) : 0);
}

// Header (kind + extra) and contents for one object.
void encodeObject(Encoder& e, Obj* o, Type* t, Vec<u8>& headers, Vec<u8>& content) {
    auto const& b = bt();
    auto tix = e.typeIndex.find(t);
    if (tix == e.typeIndex.end()) failEnc("internal: type missing from signature");
    putVarint(headers, tix->second);

    if (t == b.stringType) {
        auto* s = static_cast<StringObj*>(o);
        headers.push_back(SK_STRING);
        putVarint(headers, s->s.size());
        putBytes(content, s->s.data(), s->s.size());
        return;
    }
    if (t == b.bytesType) {
        auto* s = static_cast<BytesObj*>(o);
        headers.push_back(SK_BYTES);
        putVarint(headers, s->data.size());
        putBytes(content, s->data.data(), s->data.size());
        return;
    }
    if (t == b.fractionType) {
        auto* f = static_cast<Fraction*>(o);
        headers.push_back(SK_FRACTION);
        putVarint(headers, 0);
        putU64(content, (u64)f->r.numer());
        putU64(content, (u64)f->r.denom());
        return;
    }
    if (t == b.complexType) {
        auto* x = static_cast<Complex*>(o);
        headers.push_back(SK_COMPLEX);
        putVarint(headers, 0);
        Word w1; w1.f = x->x.real(); putU64(content, (u64)w1.i);
        Word w2; w2.f = x->x.imag(); putU64(content, (u64)w2.i);
        return;
    }
    if (auto* at = dynamic_cast<ArrayType*>(t)) {
        Type* et = at->elemType_;
        switch (arrayBackendFor(et)) {
            case ArrayBackend::Int: {
                auto* a = static_cast<PodArray<i64>*>(o);
                headers.push_back(SK_POD_ARRAY);
                putVarint(headers, a->v.size());
                for (auto v : a->v) putU64(content, (u64)v);
                return;
            }
            case ArrayBackend::Float: {
                auto* a = static_cast<PodArray<f64>*>(o);
                headers.push_back(SK_POD_ARRAY);
                putVarint(headers, a->v.size());
                for (auto v : a->v) { Word w; w.f = v; putU64(content, (u64)w.i); }
                return;
            }
            case ArrayBackend::Complex: {
                auto* a = static_cast<PodArray<x64>*>(o);
                headers.push_back(SK_POD_ARRAY);
                putVarint(headers, a->v.size());
                for (auto const& v : a->v) {
                    Word w1; w1.f = v.real(); putU64(content, (u64)w1.i);
                    Word w2; w2.f = v.imag(); putU64(content, (u64)w2.i);
                }
                return;
            }
            case ArrayBackend::Fraction: {
                auto* a = static_cast<PodArray<r64>*>(o);
                headers.push_back(SK_POD_ARRAY);
                putVarint(headers, a->v.size());
                for (auto const& v : a->v) {
                    putU64(content, (u64)v.numer());
                    putU64(content, (u64)v.denom());
                }
                return;
            }
            case ArrayBackend::Inline: {
                auto* a = static_cast<InlineArray*>(o);
                headers.push_back(SK_INLINE_ARRAY);
                putVarint(headers, a->size());
                for (size_t i = 0; i < a->size(); ++i) encodeValue(e, a->slot(i), et, content);
                return;
            }
            case ArrayBackend::Obj: {
                auto* a = static_cast<ObjArray*>(o);
                headers.push_back(SK_OBJ_ARRAY);
                putVarint(headers, a->size());
                for (size_t i = 0; i < a->size(); ++i) {
                    Word w; w.o = a->get(i);
                    encodeValue(e, &w, et, content);
                }
                return;
            }
        }
    }
    if (auto* mt = dynamic_cast<MapType*>(t)) {
        auto* m = static_cast<MapObj*>(o);
        auto co = e.canonOrder.find(o);
        if (co == e.canonOrder.end()) failEnc("internal: missing canonical order");
        headers.push_back(SK_MAP);
        putVarint(headers, co->second.size());
        for (u32 slot : co->second) {
            if (slot >= m->capacity() || m->slotState(slot) != MapObj::SlotOccupied)
                failEnc("value mutated during serialization");
            encodeValue(e, m->slotKey(slot), mt->keyType_, content);
            encodeValue(e, m->slotVal(slot), mt->valueType_, content);
        }
        return;
    }
    if (auto* st2 = dynamic_cast<SetType*>(t)) {
        auto* s = static_cast<SetObj*>(o);
        auto co = e.canonOrder.find(o);
        if (co == e.canonOrder.end()) failEnc("internal: missing canonical order");
        headers.push_back(SK_SET);
        putVarint(headers, co->second.size());
        for (u32 slot : co->second) {
            if (slot >= s->capacity() || s->slotState(slot) != SetObj::SlotOccupied)
                failEnc("value mutated during serialization");
            encodeValue(e, s->slotElem(slot), st2->elemType_, content);
        }
        return;
    }
    if (auto* lt = dynamic_cast<ListType*>(t)) {
        auto* node = static_cast<ListNode*>(o);
        headers.push_back(SK_LIST_NODE);
        putVarint(headers, 0);
        encodeValue(e, node->headData(), lt->elemType_, content);
        putVarint(content, node->tail_ ? e.idOf(node->tail_) : 0);
        return;
    }
    if (auto* rt2 = dynamic_cast<RangeType*>(t)) {
        auto* r = static_cast<RangeObj*>(o);
        headers.push_back(SK_RANGE);
        putVarint(headers, r->isInfinite_ ? 1 : 0);
        Type* et = rt2->elemType_;
        encodeValue(e, r->startData(), et, content);
        encodeValue(e, r->stepData(), et, content);
        if (!r->isInfinite_) encodeValue(e, r->endData(), et, content);
        return;
    }
    if (auto* rf = dynamic_cast<RefType*>(t)) {
        if (o->gcTag() == GCTag::InlineRef) {
            auto* ir = static_cast<InlineRef*>(o);
            headers.push_back(SK_INLINE_REF);
            putVarint(headers, 0);
            encodeValue(e, &ir->v[0], rf->elemType_, content);
        } else {
            auto* r = static_cast<RefValue*>(o);
            headers.push_back(SK_REF);
            putVarint(headers, 0);
            encodeValue(e, &r->value_, rf->elemType_, content);
        }
        return;
    }
    if (auto* tt = dynamic_cast<TupleType*>(t)) {
        auto* tup = static_cast<Tuple*>(o);
        headers.push_back(SK_TUPLE);
        putVarint(headers, tup->numFields_);
        for (u32 i = 0; i < tup->numFields_; ++i) {
            auto const& f = tt->layout_[i];
            encodeValue(e, &tup->v[f.wordOffset], f.type, content);
        }
        return;
    }
    if (auto* st = dynamic_cast<StructType*>(t)) {
        auto* s = static_cast<Struct*>(o);
        headers.push_back(SK_STRUCT);
        putVarint(headers, s->numFields_);
        for (u32 i = 0; i < s->numFields_; ++i) {
            auto const& f = st->layout_[i];
            encodeValue(e, &s->v[f.wordOffset], f.type, content);
        }
        return;
    }
    if (auto* en = dynamic_cast<EnumType*>(t)) {
        auto* ev = static_cast<Enum*>(o);
        headers.push_back(SK_ENUM);
        putVarint(headers, (u64)ev->which_);
        Type* ct = en->cases_[ev->which_].type;
        if (ct && ct != bt().voidType) encodeValue(e, &ev->v[0], ct, content);
        return;
    }
    if (auto* pv = dynamic_cast<PersistentVectorType*>(t)) {
        auto* v = static_cast<PVec*>(o);
        headers.push_back(SK_PVEC);
        putVarint(headers, v->count_);
        for (u32 i = 0; i < v->count_; ++i) encodeValue(e, v->elemAt(i), pv->elemType_, content);
        return;
    }
    if (auto* pm = dynamic_cast<PersistentMapType*>(t)) {
        auto* m = static_cast<PMap*>(o);
        auto co = e.canonOrder.find(o);
        if (co == e.canonOrder.end()) failEnc("internal: missing canonical order");
        headers.push_back(SK_PMAP);
        putVarint(headers, co->second.size());
        u32 kS = strideForType(pm->keyType_);
        auto pairs = makeVec<Word const*>();
        PMapIter it(m);
        while (Word const* pair = it.next()) pairs.push_back(pair);
        if (pairs.size() != co->second.size()) failEnc("value mutated during serialization");
        for (u32 pi : co->second) {
            encodeValue(e, pairs[pi], pm->keyType_, content);
            encodeValue(e, pairs[pi] + kS, pm->valueType_, content);
        }
        return;
    }
    failEnc("value is not serializable");
}

} // namespace

bool isSerializableType(Type* t) {
    struct Walk {
        Vec<Type*> visiting = makeVec<Type*>();
        bool ok(Type* t) {
            if (!t) return false;
            auto const& b = bt();
            if (t == b.boolType || t == b.intType || t == b.floatType
                || t == b.symbolType || t == b.stringType || t == b.bytesType
                || t == b.complexType || t == b.fractionType) return true;
            for (Type* v : visiting) if (v == t) return true;  // recursive type: being checked upstack
            visiting.push_back(t);
            bool r = okInner(t);
            visiting.pop_back();
            return r;
        }
        bool okInner(Type* t) {
            if (auto* x = dynamic_cast<RangeType*>(t)) return ok(x->elemType_);
            if (auto* x = dynamic_cast<ArrayType*>(t)) return ok(x->elemType_);
            if (auto* x = dynamic_cast<ListType*>(t))  return ok(x->elemType_);
            if (auto* x = dynamic_cast<SetType*>(t))   return ok(x->elemType_);
            if (auto* x = dynamic_cast<RefType*>(t))   return ok(x->elemType_);
            if (auto* x = dynamic_cast<PersistentVectorType*>(t)) return ok(x->elemType_);
            if (auto* x = dynamic_cast<MapType*>(t))   return ok(x->keyType_) && ok(x->valueType_);
            if (auto* x = dynamic_cast<PersistentMapType*>(t)) return ok(x->keyType_) && ok(x->valueType_);
            if (auto* x = dynamic_cast<TupleType*>(t)) {
                for (Type* ft : x->fields_) if (!ok(ft)) return false;
                return true;
            }
            if (auto* x = dynamic_cast<StructType*>(t)) {
                for (auto const& f : x->fields_) if (!ok(f.type)) return false;
                return true;
            }
            if (auto* x = dynamic_cast<EnumType*>(t)) {
                for (auto const& c : x->cases_) {
                    if (!c.type || c.type == bt().voidType) continue;
                    if (!ok(c.type)) return false;
                }
                return true;
            }
            return false;   // Function/Coroutine/Future/Actor/Any/existential/...
        }
    } w;
    return w.ok(t);
}

void buildTypeSig(Type* t, Vec<u8>& out, Vec<Type*>* typeOrder) {
    SigBuilder sb{out, typeOrder};
    sb.walk(t);
}

void serializeValue(Word const* base, Type* type, Vec<u8>& out) {
    Encoder e;

    // Signature (also fixes the type-index space).
    auto sig = makeVec<u8>();
    auto typeOrder = makeVec<Type*>();
    buildTypeSig(type, sig, &typeOrder);
    for (u32 i = 0; i < (u32)typeOrder.size(); ++i) e.typeIndex.emplace(typeOrder[i], i);

    // Pass 1: discover objects, assign ids (worklist; index i is id-1).
    discoverValue(e, base, type);
    for (size_t i = 0; i < e.objs.size(); ++i) {
        discoverObject(e, e.objs[i], e.objTypes[i]);
    }

    // Pass 2: headers + contents.
    auto headers = makeVec<u8>();
    auto content = makeVec<u8>();
    for (size_t i = 0; i < e.objs.size(); ++i) {
        encodeObject(e, e.objs[i], e.objTypes[i], headers, content);
    }

    // Assemble.
    out.push_back('T'); out.push_back('Z'); out.push_back('V'); out.push_back('1');
    putVarint(out, sig.size());
    putBytes(out, sig.data(), sig.size());
    putVarint(out, e.objs.size());
    putBytes(out, headers.data(), headers.size());
    putBytes(out, content.data(), content.size());
    encodeValue(e, base, type, out);
}

// --- decoder ---

namespace {

struct Decoder {
    Reader r;
    size_t totalLen;
    Vec<Type*> typeOrder = makeVec<Type*>();
    Vec<Obj*> objs = makeVec<Obj*>();       // id-1 -> shell
    Vec<Type*> objTypes = makeVec<Type*>();
    Vec<u8> objKinds = makeVec<u8>();
    Vec<u64> objExtras = makeVec<u64>();

    // Deferred hash-container fills (populated after all contents exist).
    struct HashFill {
        u32 id;
        Vec<Word> words;   // packed entries
        u32 count;
    };
    Vec<HashFill> hashFills = makeVec<HashFill>();

    Obj* objAt(u64 id) {
        if (id == 0) return nullptr;
        if (id > objs.size()) fail("object id out of range");
        return objs[(size_t)id - 1];
    }
};

// Decode one value slot into `dst` (strideForType(t) words).
void decodeValue(Decoder& d, Type* t, Word* dst) {
    if (t->repr_ == Type::Repr::Inline) {
        auto const& b = bt();
        if (t == b.complexType || t == b.fractionType) {
            dst[0].i = (i64)d.r.u64le();
            dst[1].i = (i64)d.r.u64le();
            return;
        }
        if (auto* tt = dynamic_cast<TupleType*>(t)) {
            for (size_t i = 0; i < tt->fields_.size(); ++i) {
                auto const& f = tt->layout_[i];
                decodeValue(d, f.type, dst + f.wordOffset);
            }
            return;
        }
        if (auto* st = dynamic_cast<StructType*>(t)) {
            for (size_t i = 0; i < st->fields_.size(); ++i) {
                auto const& f = st->layout_[i];
                decodeValue(d, f.type, dst + f.wordOffset);
            }
            return;
        }
        if (auto* en = dynamic_cast<EnumType*>(t)) {
            for (u8 i = 0; i < en->sizeWords_; ++i) dst[i].i = 0;
            u64 which = d.r.varint();
            if (which >= en->layout_.size()) fail("enum discriminant out of range");
            dst[0].i = (i64)which;
            auto const& f = en->layout_[(size_t)which];
            if (f.type && f.sizeWords > 0 && f.type != bt().voidType) {
                decodeValue(d, f.type, dst + f.wordOffset);
            }
            return;
        }
        fail("unsupported inline type");
    }
    if (t->repr_ == Type::Repr::DiscriminantEnum) {
        i64 which = (i64)d.r.u64le();
        auto* en = static_cast<EnumType*>(t);
        if (which < 0 || (size_t)which >= en->cases_.size()) fail("enum discriminant out of range");
        dst[0].i = which;
        return;
    }
    if (t->repr_ == Type::Repr::UnwrappedTupleStruct) {
        auto* st = static_cast<StructType*>(t);
        decodeValue(d, st->layout_[0].type, dst);
        return;
    }
    if (t->repr_ == Type::Repr::NullablePtrEnum) {
        dst[0].o = d.objAt(d.r.varint());
        return;
    }
    auto const& b = bt();
    if (t == b.intType || t == b.boolType || t == b.floatType) {
        dst[0].i = (i64)d.r.u64le();
        if (t == b.boolType) dst[0].i = dst[0].i ? 1 : 0;
        return;
    }
    if (t == b.symbolType) {
        u64 len = d.r.varint();
        u8 const* p = d.r.raw((size_t)len);
        dst[0].s = intern(std::string_view((char const*)p, (size_t)len));
        return;
    }
    if (!t->isObjType()) fail("unsupported type");
    dst[0].o = d.objAt(d.r.varint());
}

Obj* allocShell(Type* t, u8 kind, u64 extra, size_t totalLen) {
    auto const& b = bt();
    // Amplification guard: every element/entry/byte costs at least one
    // encoded byte, so declared sizes are bounded by the buffer length.
    if (extra > totalLen + 1) fail("declared size exceeds buffer");

    if (t == b.stringType) {
        if (kind != SK_STRING) fail("kind/type mismatch");
        auto* s = new StringObj();
        s->s.resize((size_t)extra);
        registerNewObj(s);
        return s;
    }
    if (t == b.bytesType) {
        if (kind != SK_BYTES) fail("kind/type mismatch");
        auto* s = new BytesObj();
        s->data.resize((size_t)extra);
        return s;
    }
    if (t == b.fractionType) {
        if (kind != SK_FRACTION) fail("kind/type mismatch");
        return new Fraction();
    }
    if (t == b.complexType) {
        if (kind != SK_COMPLEX) fail("kind/type mismatch");
        return new Complex();
    }
    if (auto* at = dynamic_cast<ArrayType*>(t)) {
        switch (arrayBackendFor(at->elemType_)) {
            case ArrayBackend::Int: {
                if (kind != SK_POD_ARRAY) fail("kind/type mismatch");
                auto* a = new PodArray<i64>(t); a->v.resize((size_t)extra); return a;
            }
            case ArrayBackend::Float: {
                if (kind != SK_POD_ARRAY) fail("kind/type mismatch");
                auto* a = new PodArray<f64>(t); a->v.resize((size_t)extra); return a;
            }
            case ArrayBackend::Complex: {
                if (kind != SK_POD_ARRAY) fail("kind/type mismatch");
                auto* a = new PodArray<x64>(t); a->v.resize((size_t)extra); return a;
            }
            case ArrayBackend::Fraction: {
                if (kind != SK_POD_ARRAY) fail("kind/type mismatch");
                auto* a = new PodArray<r64>(t); a->v.resize((size_t)extra); return a;
            }
            case ArrayBackend::Inline: {
                if (kind != SK_INLINE_ARRAY) fail("kind/type mismatch");
                auto* a = new InlineArray(at); a->resize((size_t)extra); return a;
            }
            case ArrayBackend::Obj: {
                if (kind != SK_OBJ_ARRAY) fail("kind/type mismatch");
                auto* a = new ObjArray(t); a->resize((size_t)extra); return a;
            }
        }
    }
    if (auto* mt = dynamic_cast<MapType*>(t)) {
        if (kind != SK_MAP) fail("kind/type mismatch");
        return new MapObj(mt);
    }
    if (auto* st2 = dynamic_cast<SetType*>(t)) {
        if (kind != SK_SET) fail("kind/type mismatch");
        return new SetObj(st2);
    }
    if (dynamic_cast<ListType*>(t)) {
        if (kind != SK_LIST_NODE) fail("kind/type mismatch");
        return ListNode::create(t);
    }
    if (auto* rt2 = dynamic_cast<RangeType*>(t)) {
        if (kind != SK_RANGE) fail("kind/type mismatch");
        return RangeObj::create(rt2, extra != 0);
    }
    if (auto* rf = dynamic_cast<RefType*>(t)) {
        if (kind == SK_INLINE_REF) return InlineRef::create(rf);
        if (kind == SK_REF) return new RefValue(rf);
        fail("kind/type mismatch");
    }
    if (auto* tt = dynamic_cast<TupleType*>(t)) {
        if (kind != SK_TUPLE || extra != tt->fields_.size()) fail("kind/type mismatch");
        return Tuple::create(tt, (u32)tt->fields_.size());
    }
    if (auto* st = dynamic_cast<StructType*>(t)) {
        if (kind != SK_STRUCT || extra != st->fields_.size()) fail("kind/type mismatch");
        return Struct::create(st, (u32)st->fields_.size());
    }
    if (auto* en = dynamic_cast<EnumType*>(t)) {
        if (kind != SK_ENUM || extra >= en->cases_.size()) fail("kind/type mismatch");
        return Enum::create(en, (int)extra);
    }
    if (auto* pv = dynamic_cast<PersistentVectorType*>(t)) {
        if (kind != SK_PVEC) fail("kind/type mismatch");
        return new PVec(pv);
    }
    if (auto* pm = dynamic_cast<PersistentMapType*>(t)) {
        if (kind != SK_PMAP) fail("kind/type mismatch");
        return new PMap(pm);
    }
    fail("unsupported type in header");
}

void fillObject(Decoder& d, u32 id) {
    Obj* o = d.objs[id - 1];
    Type* t = d.objTypes[id - 1];
    u8 kind = d.objKinds[id - 1];
    u64 extra = d.objExtras[id - 1];
    auto const& b = bt();

    switch (kind) {
        case SK_STRING: {
            auto* s = static_cast<StringObj*>(o);
            u8 const* p = d.r.raw((size_t)extra);
            std::memcpy(s->s.data(), p, (size_t)extra);
            return;
        }
        case SK_BYTES: {
            auto* s = static_cast<BytesObj*>(o);
            u8 const* p = d.r.raw((size_t)extra);
            std::memcpy(s->data.data(), p, (size_t)extra);
            return;
        }
        case SK_FRACTION: {
            auto* f = static_cast<Fraction*>(o);
            i64 n = (i64)d.r.u64le();
            i64 dd = (i64)d.r.u64le();
            f->r = r64(n, dd);
            return;
        }
        case SK_COMPLEX: {
            auto* x = static_cast<Complex*>(o);
            Word w1; w1.i = (i64)d.r.u64le();
            Word w2; w2.i = (i64)d.r.u64le();
            x->x = x64(w1.f, w2.f);
            return;
        }
        case SK_POD_ARRAY: {
            auto* at = static_cast<ArrayType*>(t);
            switch (arrayBackendFor(at->elemType_)) {
                case ArrayBackend::Int: {
                    auto* a = static_cast<PodArray<i64>*>(o);
                    for (auto& v : a->v) v = (i64)d.r.u64le();
                    return;
                }
                case ArrayBackend::Float: {
                    auto* a = static_cast<PodArray<f64>*>(o);
                    for (auto& v : a->v) { Word w; w.i = (i64)d.r.u64le(); v = w.f; }
                    return;
                }
                case ArrayBackend::Complex: {
                    auto* a = static_cast<PodArray<x64>*>(o);
                    for (auto& v : a->v) {
                        Word w1; w1.i = (i64)d.r.u64le();
                        Word w2; w2.i = (i64)d.r.u64le();
                        v = x64(w1.f, w2.f);
                    }
                    return;
                }
                case ArrayBackend::Fraction: {
                    auto* a = static_cast<PodArray<r64>*>(o);
                    for (auto& v : a->v) {
                        i64 n = (i64)d.r.u64le();
                        i64 dd = (i64)d.r.u64le();
                        v = r64(n, dd);
                    }
                    return;
                }
                default: fail("kind/type mismatch");
            }
        }
        case SK_OBJ_ARRAY: {
            auto* at = static_cast<ArrayType*>(t);
            auto* a = static_cast<ObjArray*>(o);
            for (size_t i = 0; i < a->size(); ++i) {
                Word w;
                decodeValue(d, at->elemType_, &w);
                a->rawVec()[i] = w.o;
            }
            return;
        }
        case SK_INLINE_ARRAY: {
            auto* at = static_cast<ArrayType*>(t);
            auto* a = static_cast<InlineArray*>(o);
            for (size_t i = 0; i < a->size(); ++i) {
                decodeValue(d, at->elemType_, a->slot(i));
            }
            return;
        }
        case SK_MAP: {
            auto* mt = static_cast<MapType*>(t);
            auto* m = static_cast<MapObj*>(o);
            u32 kS = m->keyStride_;
            u32 vS = m->valueStride_;
            Decoder::HashFill hf{id, makeVec<Word>(), (u32)extra};
            hf.words.resize((size_t)extra * (kS + vS));
            for (u64 i = 0; i < extra; ++i) {
                decodeValue(d, mt->keyType_, hf.words.data() + i * (kS + vS));
                decodeValue(d, mt->valueType_, hf.words.data() + i * (kS + vS) + kS);
            }
            d.hashFills.push_back(std::move(hf));
            return;
        }
        case SK_SET: {
            auto* st2 = static_cast<SetType*>(t);
            auto* s = static_cast<SetObj*>(o);
            u32 eS = s->elemStride_;
            Decoder::HashFill hf{id, makeVec<Word>(), (u32)extra};
            hf.words.resize((size_t)extra * eS);
            for (u64 i = 0; i < extra; ++i) {
                decodeValue(d, st2->elemType_, hf.words.data() + i * eS);
            }
            d.hashFills.push_back(std::move(hf));
            return;
        }
        case SK_LIST_NODE: {
            auto* lt = static_cast<ListType*>(t);
            auto* node = static_cast<ListNode*>(o);
            decodeValue(d, lt->elemType_, node->headData());
            u64 tailId = d.r.varint();
            Obj* tail = d.objAt(tailId);
            if (tail && tail->gcTag() != GCTag::ListNode) fail("list tail is not a list node");
            node->tail_ = static_cast<ListNode*>(tail);
            return;
        }
        case SK_RANGE: {
            auto* rt2 = static_cast<RangeType*>(t);
            auto* r = static_cast<RangeObj*>(o);
            Type* et = rt2->elemType_;
            decodeValue(d, et, r->startData());
            decodeValue(d, et, r->stepData());
            if (!r->isInfinite_) decodeValue(d, et, r->endData());
            return;
        }
        case SK_REF: {
            auto* rf = static_cast<RefType*>(t);
            auto* r = static_cast<RefValue*>(o);
            decodeValue(d, rf->elemType_, &r->value_);
            return;
        }
        case SK_INLINE_REF: {
            auto* rf = static_cast<RefType*>(t);
            auto* ir = static_cast<InlineRef*>(o);
            decodeValue(d, rf->elemType_, &ir->v[0]);
            return;
        }
        case SK_TUPLE: {
            auto* tt = static_cast<TupleType*>(t);
            auto* tup = static_cast<Tuple*>(o);
            for (u32 i = 0; i < tup->numFields_; ++i) {
                auto const& f = tt->layout_[i];
                decodeValue(d, f.type, &tup->v[f.wordOffset]);
            }
            return;
        }
        case SK_STRUCT: {
            auto* st = static_cast<StructType*>(t);
            auto* s = static_cast<Struct*>(o);
            for (u32 i = 0; i < s->numFields_; ++i) {
                auto const& f = st->layout_[i];
                decodeValue(d, f.type, &s->v[f.wordOffset]);
            }
            return;
        }
        case SK_ENUM: {
            auto* en = static_cast<EnumType*>(t);
            auto* ev = static_cast<Enum*>(o);
            Type* ct = en->cases_[ev->which_].type;
            if (ct && ct != b.voidType) decodeValue(d, ct, &ev->v[0]);
            return;
        }
        case SK_PVEC: {
            auto* pv = static_cast<PersistentVectorType*>(t);
            auto* shell = static_cast<PVec*>(o);
            u32 stride = strideForType(pv->elemType_);
            auto elems = makeVec<Word>();
            elems.resize((size_t)extra * stride);
            for (u64 i = 0; i < extra; ++i) {
                decodeValue(d, pv->elemType_, elems.data() + i * stride);
            }
            PVec* built = PVec::fromWords(pv, elems.data(), (u32)extra);
            shell->count_ = built->count_;
            shell->shift_ = built->shift_;
            shell->stride_ = built->stride_;
            shell->root_ = built->root_;
            shell->tail_ = built->tail_;
            return;
        }
        case SK_PMAP: {
            auto* pm = static_cast<PersistentMapType*>(t);
            u32 kS = strideForType(pm->keyType_);
            u32 vS = strideForType(pm->valueType_);
            Decoder::HashFill hf{id, makeVec<Word>(), (u32)extra};
            hf.words.resize((size_t)extra * (kS + vS));
            for (u64 i = 0; i < extra; ++i) {
                decodeValue(d, pm->keyType_, hf.words.data() + i * (kS + vS));
                decodeValue(d, pm->valueType_, hf.words.data() + i * (kS + vS) + kS);
            }
            d.hashFills.push_back(std::move(hf));
            return;
        }
        default: fail("unknown object kind");
    }
}

} // namespace

void deserializeValue(u8 const* data, size_t len, Type* type, Word* dst) {
    Decoder d;
    d.r = Reader{data, data + len};
    d.totalLen = len;

    // Magic + signature validation.
    u8 const* magic = d.r.raw(4);
    if (magic[0] != 'T' || magic[1] != 'Z' || magic[2] != 'V' || magic[3] != '1') {
        fail("bad magic (not a TZV1 buffer)");
    }
    u64 sigLen = d.r.varint();
    u8 const* sig = d.r.raw((size_t)sigLen);
    auto expectSig = makeVec<u8>();
    buildTypeSig(type, expectSig, &d.typeOrder);
    if (sigLen != expectSig.size()
        || std::memcmp(sig, expectSig.data(), (size_t)sigLen) != 0) {
        fail("type signature mismatch");
    }

    // Headers: allocate all shells.
    u64 nObjs = d.r.varint();
    if (nObjs > len) fail("declared object count exceeds buffer");
    for (u64 i = 0; i < nObjs; ++i) {
        u64 typeIdx = d.r.varint();
        if (typeIdx >= d.typeOrder.size()) fail("type index out of range");
        u8 kind = d.r.u8v();
        u64 extra = d.r.varint();
        Type* t = d.typeOrder[(size_t)typeIdx];
        Obj* shell = allocShell(t, kind, extra, len);
        d.objs.push_back(shell);
        d.objTypes.push_back(t);
        d.objKinds.push_back(kind);
        d.objExtras.push_back(extra);
    }

    // Contents in id order (hash containers deferred).
    for (u64 i = 1; i <= nObjs; ++i) fillObject(d, (u32)i);

    // Populate hash containers over final contents. Decreasing id order:
    // an inner container gets its entries before any outer container needs
    // to hash it as part of a key.
    for (size_t i = d.hashFills.size(); i-- > 0;) {
        auto& hf = d.hashFills[i];
        Obj* o = d.objs[hf.id - 1];
        Type* t = d.objTypes[hf.id - 1];
        if (auto* mt = dynamic_cast<MapType*>(t)) {
            auto* m = static_cast<MapObj*>(o);
            u32 sS = m->slotStride();
            for (u32 j = 0; j < hf.count; ++j) {
                Word const* entry = hf.words.data() + (size_t)j * sS;
                if (m->findSlot(entry) != m->capacity()) fail("duplicate map key");
                m->insertNew(entry, entry + m->keyStride_);
            }
        } else if (dynamic_cast<SetType*>(t)) {
            auto* s = static_cast<SetObj*>(o);
            u32 eS = s->elemStride_;
            for (u32 j = 0; j < hf.count; ++j) {
                Word const* elem = hf.words.data() + (size_t)j * eS;
                if (s->findSlot(elem) != s->capacity()) fail("duplicate set element");
                s->insertNew(elem);
            }
        } else if (auto* pm = dynamic_cast<PersistentMapType*>(t)) {
            auto* shell = static_cast<PMap*>(o);
            PMap* built = PMap::fromPairs(pm, hf.words.data(), hf.count);
            shell->count_ = built->count_;
            shell->root_ = built->root_;
        } else {
            fail("internal: bad deferred fill");
        }
    }

    // Root value.
    decodeValue(d, type, dst);
}

} // namespace ts
