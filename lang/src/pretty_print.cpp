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
//  pretty_print.cpp
//  lang
//
//  Doc IR + strict Wadler/Lindig layout + value-to-Doc builder.
//
//  Doc grammar: Text (verbatim), Line (newline+indent, or its flat text
//  when the enclosing Group fits), Concat, Nest (adds indent to contained
//  Lines), Group (fits-on-one-line decision point). Nodes live in a flat
//  arena (indices, no per-node allocation); layout is iterative.
//

#include "pretty_print.hpp"
#include "value_graph.hpp"
#include "persistent_vector.hpp"
#include "persistent_map.hpp"

#include <cmath>
#include <string_view>

namespace ts {

namespace {

enum class DK : u8 { Text, Line, Concat, Nest, Group };

struct DocNode {
    DK  k;
    i32 indent = 0;          // Nest
    u32 a = 0, b = 0;        // children (Concat: a,b; Nest/Group: a)
    u32 off = 0, len = 0;    // Text payload / Line flat text, into chars
};

constexpr i32 kIndentStep = 2;

class DocBuilder {
public:
    Vec<DocNode> nodes;
    VMString chars;

    DocBuilder()
        : nodes(rt::STLAllocator<DocNode>(rt::gCurrentAllocator))
        , chars(rt::STLAllocator<char>(rt::gCurrentAllocator)) {}

    u32 text(std::string_view s) {
        DocNode n; n.k = DK::Text;
        n.off = (u32)chars.size(); n.len = (u32)s.size();
        chars.append(s.data(), s.size());
        nodes.push_back(n);
        return (u32)nodes.size() - 1;
    }
    // A line break that renders as `flat` when the enclosing group fits.
    u32 line(std::string_view flat) {
        DocNode n; n.k = DK::Line;
        n.off = (u32)chars.size(); n.len = (u32)flat.size();
        chars.append(flat.data(), flat.size());
        nodes.push_back(n);
        return (u32)nodes.size() - 1;
    }
    u32 concat(u32 a, u32 b) {
        DocNode n; n.k = DK::Concat; n.a = a; n.b = b;
        nodes.push_back(n);
        return (u32)nodes.size() - 1;
    }
    u32 nest(i32 indent, u32 a) {
        DocNode n; n.k = DK::Nest; n.indent = indent; n.a = a;
        nodes.push_back(n);
        return (u32)nodes.size() - 1;
    }
    u32 group(u32 a) {
        DocNode n; n.k = DK::Group; n.a = a;
        nodes.push_back(n);
        return (u32)nodes.size() - 1;
    }
};

// join items with "," + line(" "), wrap in open/close with soft breaks:
// flat  -> open i0, i1, ... close
// break -> open\n  i0,\n  i1\n close      (2-space nest)
u32 bracketed(DocBuilder& b, std::string_view open, std::string_view close,
              Vec<u32> const& items) {
    if (items.empty()) {
        VMString s = rt::vmstr(open);
        s.append(close.data(), close.size());
        return b.text(std::string_view(s.data(), s.size()));
    }
    u32 body = items[0];
    for (size_t i = 1; i < items.size(); ++i) {
        body = b.concat(body, b.concat(b.text(","), b.concat(b.line(" "), items[i])));
    }
    u32 doc = b.concat(b.text(open), b.nest(kIndentStep, b.concat(b.line(""), body)));
    doc = b.concat(doc, b.concat(b.line(""), b.text(close)));
    return b.group(doc);
}

// Same but the flat form keeps a space after `open` and before `close`
// ("Name { x: 1 }" style).
u32 bracketedSpaced(DocBuilder& b, std::string_view open, std::string_view close,
                    Vec<u32> const& items) {
    if (items.empty()) {
        VMString s = rt::vmstr(open);
        s += " ";
        s.append(close.data(), close.size());
        return b.text(std::string_view(s.data(), s.size()));
    }
    u32 body = items[0];
    for (size_t i = 1; i < items.size(); ++i) {
        body = b.concat(body, b.concat(b.text(","), b.concat(b.line(" "), items[i])));
    }
    u32 doc = b.concat(b.text(open), b.nest(kIndentStep, b.concat(b.line(" "), body)));
    doc = b.concat(doc, b.concat(b.line(" "), b.text(close)));
    return b.group(doc);
}

Vec<u32> makeItems() {
    return Vec<u32>(rt::STLAllocator<u32>(rt::gCurrentAllocator));
}

u32 buildWordsDoc(DocBuilder& b, Word const* base, Type* type);
u32 buildWordDoc(DocBuilder& b, Word w, Type* type);

u32 textVM(DocBuilder& b, VMString const& s) {
    return b.text(std::string_view(s.data(), s.size()));
}

u32 cycleMarker(DocBuilder& b, PrintCycleScope const& pcs) {
    VMString s = rt::fmt("^{}^", pcs.levelsUp);
    return textVM(b, s);
}

// Tuple contents "(a, b)" / 1-tuple "(a,)" from an inline payload; used by
// heap Tuple, inline tuples, and enum tuple-case payloads.
u32 buildTupleDoc(DocBuilder& b, Word const* base, TupleType* tt, u32 numFields) {
    auto items = makeItems();
    for (u32 i = 0; i < numFields; ++i) {
        auto const& f = tt->layout_[i];
        items.push_back(buildWordsDoc(b, base + f.wordOffset, f.type));
    }
    if (numFields == 1) {
        // "(x,)" -- fold the trailing comma into the single item.
        items[0] = b.concat(items[0], b.text(","));
    }
    return bracketed(b, "(", ")", items);
}

// Struct contents from an inline payload: "Name(a, b)" (tuple struct) or
// "Name { x: 1 }" (named).
u32 buildStructDoc(DocBuilder& b, Word const* base, StructType* st, u32 numFields) {
    VMString name = rt::vmstr(st->name_->str());
    if (st->isTupleStruct_) {
        auto items = makeItems();
        for (u32 i = 0; i < numFields; ++i) {
            auto const& f = st->layout_[i];
            items.push_back(buildWordsDoc(b, base + f.wordOffset, f.type));
        }
        name += "(";
        return bracketed(b, std::string_view(name.data(), name.size()), ")", items);
    }
    auto items = makeItems();
    for (u32 i = 0; i < numFields; ++i) {
        auto const& f = st->layout_[i];
        VMString label = rt::vmstr(st->fields_[i].name->str());
        label += ": ";
        items.push_back(b.concat(textVM(b, label),
                                 buildWordsDoc(b, base + f.wordOffset, f.type)));
    }
    name += " {";
    return bracketedSpaced(b, std::string_view(name.data(), name.size()), "}", items);
}

// Enum contents from a payload: "Name.case", "Name.case(payload)", or
// "Name.case(a, b)" for tuple-case payloads (no double parens).
u32 buildEnumDoc(DocBuilder& b, EnumType* en, int which, Word const* payload) {
    VMString head = rt::vmstr(en->name_->str());
    head += ".";
    if (which < 0 || (size_t)which >= en->cases_.size()) {
        head += "?";
        return textVM(b, head);
    }
    head += en->cases_[which].name->str();
    auto const& f = en->layout_[which];
    if (!f.type || f.sizeWords == 0) return textVM(b, head);
    bool isVoid = !f.type->isObjType()
               && (dynamic_cast<VoidType*>(f.type) != nullptr);
    if (isVoid) return textVM(b, head);
    auto items = makeItems();
    if (auto* tt = dynamic_cast<TupleType*>(f.type)) {
        for (size_t i = 0; i < tt->fields_.size(); ++i) {
            auto const& tf = tt->layout_[i];
            items.push_back(buildWordsDoc(b, payload + tf.wordOffset, tf.type));
        }
        if (tt->fields_.size() == 1) items[0] = b.concat(items[0], b.text(","));
    } else {
        items.push_back(buildWordsDoc(b, payload, f.type));
    }
    head += "(";
    return bracketed(b, std::string_view(head.data(), head.size()), ")", items);
}

// Heap-object dispatch (w.o non-null, `type` an Obj-repr static type).
u32 buildObjDoc(DocBuilder& b, Word w, Type* type) {
    if (type == gCurrentVM->stringType()) {
        return textVM(b, w.o->str());   // bare contents, as the flat printer
    }
    if (auto* arrT = dynamic_cast<ArrayType*>(type)) {
        Type* et = arrT->elemType_;
        switch (arrayBackendFor(et)) {
            case ArrayBackend::Inline: {
                auto* a = static_cast<InlineArray*>(w.o);
                PrintCycleScope pcs(w.o);
                if (pcs.cycled) return cycleMarker(b, pcs);
                if (pcs.tooDeep) return b.text("...");
                auto items = makeItems();
                for (size_t i = 0; i < a->size(); ++i) {
                    items.push_back(buildWordsDoc(b, a->slot(i), et));
                }
                return bracketed(b, "[", "]", items);
            }
            case ArrayBackend::Obj: {
                auto* a = static_cast<ObjArray*>(w.o);
                PrintCycleScope pcs(w.o);
                if (pcs.cycled) return cycleMarker(b, pcs);
                if (pcs.tooDeep) return b.text("...");
                auto items = makeItems();
                for (size_t i = 0; i < a->size(); ++i) {
                    Word ew; ew.o = a->get(i);
                    items.push_back(buildWordDoc(b, ew, et));
                }
                return bracketed(b, "[", "]", items);
            }
            default: {
                // POD backends (Int/Float/Complex/Fraction): leaf elements.
                auto items = makeItems();
                if (et == gCurrentVM->complexType()) {
                    auto* a = static_cast<PodArray<x64>*>(w.o);
                    for (auto const& v : a->v) {
                        f64 re = v.real(), im = v.imag();
                        VMString s = std::signbit(im) ? rt::fmt("{}{}i", re, im)
                                                      : rt::fmt("{}+{}i", re, im);
                        items.push_back(textVM(b, s));
                    }
                } else if (et == gCurrentVM->fractionType()) {
                    auto* a = static_cast<PodArray<r64>*>(w.o);
                    for (auto const& v : a->v) {
                        items.push_back(textVM(b, rt::fmt("{}/{}", v.numer(), v.denom())));
                    }
                } else if (et == gCurrentVM->floatType()) {
                    auto* a = static_cast<PodArray<f64>*>(w.o);
                    for (auto val : a->v) {
                        Word ew; ew.f = val;
                        items.push_back(textVM(b, wordToString(ew, et)));
                    }
                } else {
                    auto* a = static_cast<PodArray<i64>*>(w.o);
                    for (auto val : a->v) {
                        Word ew; ew.i = val;
                        items.push_back(textVM(b, wordToString(ew, et)));
                    }
                }
                return bracketed(b, "[", "]", items);
            }
        }
    }
    if (dynamic_cast<MapType*>(type)) {
        auto* m = static_cast<MapObj*>(w.o);
        PrintCycleScope pcs(w.o);
        if (pcs.cycled) return cycleMarker(b, pcs);
        if (pcs.tooDeep) return b.text("...");
        if (m->empty()) return b.text("[:]");
        Type* kt = m->keyType();
        Type* vt = m->valueType();
        auto items = makeItems();
        u32 cap = m->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (m->slotState(i) != MapObj::SlotOccupied) continue;
            u32 entry = buildWordsDoc(b, m->slotKey(i), kt);
            entry = b.concat(entry, b.text(": "));
            entry = b.concat(entry, buildWordsDoc(b, m->slotVal(i), vt));
            items.push_back(entry);
        }
        return bracketed(b, "[", "]", items);
    }
    if (dynamic_cast<SetType*>(type)) {
        auto* s = static_cast<SetObj*>(w.o);
        PrintCycleScope pcs(w.o);
        if (pcs.cycled) return cycleMarker(b, pcs);
        if (pcs.tooDeep) return b.text("...");
        Type* et = s->elemType();
        auto items = makeItems();
        u32 cap = s->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (s->slotState(i) != SetObj::SlotOccupied) continue;
            items.push_back(buildWordsDoc(b, s->slotElem(i), et));
        }
        return bracketed(b, "Set(", ")", items);
    }
    if (auto* listT = dynamic_cast<ListType*>(type)) {
        Type* et = listT->elemType_;
        i64 limit = gCurrentVM->listPrintLimit();
        auto* node = static_cast<ListNode*>(w.o);
        auto items = makeItems();
        i64 count = 0;
        while (node) {
            {
                GraphNeutralScope neutral;
                node->force(*gCurrentVM);
            }
            if (count >= limit) {
                items.push_back(b.text("..."));
                break;
            }
            items.push_back(buildWordsDoc(b, node->headData(), et));
            ++count;
            node = node->tail_;
        }
        return bracketed(b, "List(", ")", items);
    }
    if (auto* refT = dynamic_cast<RefType*>(type)) {
        PrintCycleScope pcs(w.o);
        if (pcs.cycled) return cycleMarker(b, pcs);
        if (pcs.tooDeep) return b.text("...");
        auto items = makeItems();
        if (w.o->gcTag() == GCTag::InlineRef) {
            auto* ir = static_cast<InlineRef*>(w.o);
            items.push_back(buildWordsDoc(b, &ir->v[0], refT->elemType_));
        } else {
            auto* r = static_cast<RefValue*>(w.o);
            items.push_back(buildWordDoc(b, r->value_, refT->elemType_));
        }
        return bracketed(b, "Ref(", ")", items);
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* tup = static_cast<Tuple*>(w.o);
        return buildTupleDoc(b, &tup->v[0], tt, tup->numFields_);
    }
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* s = static_cast<Struct*>(w.o);
        return buildStructDoc(b, &s->v[0], st, s->numFields_);
    }
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        auto* e = static_cast<Enum*>(w.o);
        return buildEnumDoc(b, en, e->which_, &e->v[0]);
    }
    if (auto* pvT = dynamic_cast<PersistentVectorType*>(type)) {
        auto* v = static_cast<PVec*>(w.o);
        if (v->count_ == 0) return b.text("#[]");
        Type* et = pvT->elemType_;
        auto items = makeItems();
        for (u32 i = 0; i < v->count_; ++i) {
            items.push_back(buildWordsDoc(b, v->elemAt(i), et));
        }
        return bracketed(b, "#[", "]", items);
    }
    if (auto* pmT = dynamic_cast<PersistentMapType*>(type)) {
        auto* m = static_cast<PMap*>(w.o);
        if (m->count_ == 0) return b.text("#[:]");
        Type* kt = pmT->keyType_;
        Type* vt = pmT->valueType_;
        u32 kS = strideForType(kt);
        auto items = makeItems();
        PMapIter it(m);
        while (Word const* pair = it.next()) {
            u32 entry = buildWordsDoc(b, pair, kt);
            entry = b.concat(entry, b.text(": "));
            entry = b.concat(entry, buildWordsDoc(b, pair + kS, vt));
            items.push_back(entry);
        }
        return bracketed(b, "#[", "]", items);
    }
    // Leaf objects (String handled above; Range, Bytes, Fraction, Complex,
    // Any, existentials, callables, ...): flat text.
    return textVM(b, w.o->str());
}

u32 buildWordDoc(DocBuilder& b, Word w, Type* type) {
    // Special reprs first, mirroring wordToString.
    if (type && type->repr_ == Type::Repr::UnwrappedTupleStruct) {
        if (auto* st = dynamic_cast<StructType*>(type); st && !st->layout_.empty()) {
            VMString head = rt::vmstr(st->name_->str());
            head += "(";
            auto items = makeItems();
            items.push_back(buildWordDoc(b, w, st->layout_[0].type));
            return bracketed(b, std::string_view(head.data(), head.size()), ")", items);
        }
    }
    if (type && type->repr_ == Type::Repr::NullablePtrEnum) {
        if (auto* et = dynamic_cast<EnumType*>(type)) {
            int voidIdx = nullablePtrVoidCaseIndex(et);
            int dataIdx = (voidIdx == 0) ? 1 : 0;
            if (!w.o) return textVM(b, wordToString(w, type));
            VMString head = rt::vmstr(et->name_->str());
            head += ".";
            head += et->cases_[dataIdx].name->str();
            head += "(";
            auto items = makeItems();
            items.push_back(buildWordDoc(b, w, et->cases_[dataIdx].type));
            return bracketed(b, std::string_view(head.data(), head.size()), ")", items);
        }
    }
    if (!type || !type->isObjType()
        || type->repr_ == Type::Repr::DiscriminantEnum) {
        return textVM(b, wordToString(w, type));   // atoms and tag-only enums
    }
    if (!w.o) return b.text("nil");
    return buildObjDoc(b, w, type);
}

u32 buildWordsDoc(DocBuilder& b, Word const* base, Type* type) {
    if (!type) return b.text("nil");
    if (type->repr_ != Type::Repr::Inline) {
        return buildWordDoc(b, base[0], type);
    }
    if (type == gCurrentVM->complexType() || type == gCurrentVM->fractionType()) {
        return textVM(b, wordsToString(base, type));
    }
    if (auto* st = dynamic_cast<StructType*>(type)) {
        return buildStructDoc(b, base, st, (u32)st->fields_.size());
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        return buildTupleDoc(b, base, tt, (u32)tt->fields_.size());
    }
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        int which = (int)base[0].i;
        Word const* payload = base;
        if (which >= 0 && (size_t)which < en->layout_.size()) {
            payload = base + en->layout_[which].wordOffset;
        }
        return buildEnumDoc(b, en, which, payload);
    }
    return buildWordDoc(b, base[0], type);
}

// --- layout (strict Wadler / Lindig, iterative) ---

struct LayoutItem {
    u32 node;
    i32 indent;
    bool flat;
};

bool fits(DocBuilder const& b, i64 remaining, Vec<LayoutItem> const& pending,
          LayoutItem first) {
    // Scan `first` then the rest of the pending work until the line ends
    // (a Line in break mode) or the budget is exhausted.
    Vec<LayoutItem> st{rt::STLAllocator<LayoutItem>(rt::gCurrentAllocator)};
    st = pending;
    st.push_back(first);
    while (!st.empty() && remaining >= 0) {
        LayoutItem it = st.back();
        st.pop_back();
        DocNode const& n = b.nodes[it.node];
        switch (n.k) {
            case DK::Text:   remaining -= n.len; break;
            case DK::Line:
                if (it.flat) { remaining -= n.len; break; }
                return true;   // reached the next hard break: line fits
            case DK::Concat:
                st.push_back({n.b, it.indent, it.flat});
                st.push_back({n.a, it.indent, it.flat});
                break;
            case DK::Nest:
                st.push_back({n.a, it.indent + n.indent, it.flat});
                break;
            case DK::Group:
                st.push_back({n.a, it.indent, true});
                break;
        }
    }
    return remaining >= 0;
}

VMString layout(DocBuilder const& b, u32 root, i32 width) {
    VMString out = rt::vmstr("");
    Vec<LayoutItem> st{rt::STLAllocator<LayoutItem>(rt::gCurrentAllocator)};
    st.push_back({root, 0, false});
    i64 col = 0;
    while (!st.empty()) {
        LayoutItem it = st.back();
        st.pop_back();
        DocNode const& n = b.nodes[it.node];
        switch (n.k) {
            case DK::Text:
                out.append(b.chars.data() + n.off, n.len);
                col += n.len;
                break;
            case DK::Line:
                if (it.flat) {
                    out.append(b.chars.data() + n.off, n.len);
                    col += n.len;
                } else {
                    out += '\n';
                    out.append((size_t)it.indent, ' ');
                    col = it.indent;
                }
                break;
            case DK::Concat:
                st.push_back({n.b, it.indent, it.flat});
                st.push_back({n.a, it.indent, it.flat});
                break;
            case DK::Nest:
                st.push_back({n.a, it.indent + n.indent, it.flat});
                break;
            case DK::Group: {
                bool flat = it.flat
                    || fits(b, (i64)width - col, st, {n.a, it.indent, true});
                st.push_back({n.a, it.indent, flat});
                break;
            }
        }
    }
    return out;
}

} // namespace

VMString prettyString(Word const* base, Type* type, i32 width) {
    DocBuilder b;
    u32 root = buildWordsDoc(b, base, type);
    return layout(b, root, width);
}

} // namespace ts
