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
//  type_system.cpp
//  tiny-static-2
//
//  Type implementations
//

#include "type_system.hpp"
#include "vm.hpp"

#include <unordered_set>

namespace ts {

// Type constructor
Type::Type() : Obj(gCurrentTypeUniverse->types().typeType) {}

// EnumType constructor
EnumType::EnumType(SymbolPtr name, NameTypePairVec cases)
    : name_(name)
    , layout_(rt::STLAllocator<FieldLayout>(rt::gCurrentAllocator))
{
    registerNewObj(this);
    setCases(std::move(cases));
}

void EnumType::setCases(NameTypePairVec cases) {
    cases_ = std::move(cases);
    // Phase 4c: classifyType populates layout_ with one entry per case;
    // runtime walkers select layout_[which_] and dispatch on storesObjPtr().
    classifyType(this);
}

// StructType constructor
StructType::StructType(SymbolPtr name, NameTypePairVec fields, bool isTupleStruct)
    : name_(name)
    , isTupleStruct_(isTupleStruct)
    , layout_(rt::STLAllocator<FieldLayout>(rt::gCurrentAllocator))
{
    registerNewObj(this);
    setFields(std::move(fields));
}

void StructType::setFields(NameTypePairVec fields) {
    fields_ = std::move(fields);
    // Phase 4c: classifyType populates layout_ with one entry per field.
    classifyType(this);
}

// TupleType constructor
TupleType::TupleType(TypeVec fields)
    : fields_(std::move(fields))
    , layout_(rt::STLAllocator<FieldLayout>(rt::gCurrentAllocator))
{
    registerNewObj(this);
    // Phase 4c: classifyType populates layout_ with one entry per field.
    classifyType(this);
}

// FunctionType constructor
FunctionType::FunctionType(TypeVec argTypes, Type* returnType)
    : argTypes_(std::move(argTypes))
    , returnType_(returnType)
{
    registerNewObj(this);
    classifyType(this);
}

// LambdaType constructor
LambdaType::LambdaType(TypeVec argTypes, Type* returnType, TypeVec freeVarTypes)
    : FunctionType(std::move(argTypes), returnType)
    , freeVarTypes_(std::move(freeVarTypes))
    , gcFreeVars_(rt::STLAllocator<int>(rt::gCurrentAllocator))
    , freeVarIsUpvar_(rt::STLAllocator<u8>(rt::gCurrentAllocator))
    , freeVarOffsets_(rt::STLAllocator<u16>(rt::gCurrentAllocator))
{
    registerNewObj(this);
    classifyType(this);
    // Default: every capture is byValue (legacy single-word-per-capture
    // layout). setCaptureLayout() overrides this once the codegen knows
    // which captures are upvars.
    setCaptureLayout({});
}

// Recompute capture layout (offsets, total word count, GC mask of Obj*-
// holding word offsets) for the current freeVarTypes_ and a parallel list
// of byReference flags. byReference captures take 1 word (an UpVar* Obj*);
// byValue captures take type->sizeWords_ words and contribute Obj* offsets
// for every object-bearing word in their inline layout (currently: 1-word
// Obj* types, and inline composite types that happen to be Obj*-free like
// Complex / Fraction; deeper inline layouts walk via gcScanInlinePointers
// at runtime instead of being statically enumerated here).
void LambdaType::setCaptureLayout(const std::vector<bool>& isUpvar) {
    freeVarIsUpvar_.clear();
    freeVarOffsets_.clear();
    gcFreeVars_.clear();
    u16 offset = 0;
    for (size_t i = 0; i < freeVarTypes_.size(); ++i) {
        bool byRef = (i < isUpvar.size()) && isUpvar[i];
        freeVarIsUpvar_.push_back(byRef ? (u8)1 : (u8)0);
        freeVarOffsets_.push_back(offset);
        Type* t = freeVarTypes_[i];
        if (byRef) {
            // UpVar* is always an Obj pointer; trace its single word.
            gcFreeVars_.push_back((int)offset);
            offset += 1;
        } else {
            u16 sw = (t && t->sizeWords_ > 0) ? (u16)t->sizeWords_ : (u16)1;
            // For 1-word captures, defer to storesObjPtr. For multi-word
            // inline captures (Complex / Fraction etc.) the embedded words
            // are primitive (no Obj*); nested Tuple/Struct/Enum captures
            // with object fields are not yet enumerated here -- the open
            // path keeps them reachable via stack maps and the closed path
            // would need a layout walk to be precise. This matches the
            // pre-upvar behavior for non-trivial inline captures.
            if (sw == 1 && storesObjPtr(t)) {
                gcFreeVars_.push_back((int)offset);
            }
            offset += sw;
        }
    }
    totalFreeVarWords_ = offset;
}

// TemplateLambdaType constructor
TemplateLambdaType::TemplateLambdaType(LambdaExprNode* node, TypeVec freeVarTypes,
                                       std::vector<std::string> typeParams,
                                       std::vector<ConstraintEntry> constraints)
    : astNode_(node)
    , freeVarTypes_(std::move(freeVarTypes))
    , gcFreeVars_(rt::STLAllocator<int>(rt::gCurrentAllocator))
    , freeVarIsUpvar_(rt::STLAllocator<u8>(rt::gCurrentAllocator))
    , freeVarOffsets_(rt::STLAllocator<u16>(rt::gCurrentAllocator))
    , typeParams_(std::move(typeParams))
    , constraints_(std::move(constraints))
{
    registerNewObj(this);
    classifyType(this);
    // Default: legacy single-word-per-capture layout. Codegen overrides
    // via the per-LambdaExprNode captures vector at definition time.
    u16 offset = 0;
    for (size_t i = 0; i < freeVarTypes_.size(); ++i) {
        freeVarIsUpvar_.push_back((u8)0);
        freeVarOffsets_.push_back(offset);
        Type* t = freeVarTypes_[i];
        u16 sw = (t && t->sizeWords_ > 0) ? (u16)t->sizeWords_ : (u16)1;
        if (sw == 1 && storesObjPtr(t)) {
            gcFreeVars_.push_back((int)offset);
        }
        offset += sw;
    }
    totalFreeVarWords_ = offset;
}

void TemplateLambdaType::setCaptureLayout(const std::vector<bool>& isUpvar) {
    freeVarIsUpvar_.clear();
    freeVarOffsets_.clear();
    gcFreeVars_.clear();
    u16 offset = 0;
    for (size_t i = 0; i < freeVarTypes_.size(); ++i) {
        bool byRef = (i < isUpvar.size()) && isUpvar[i];
        freeVarIsUpvar_.push_back(byRef ? (u8)1 : (u8)0);
        freeVarOffsets_.push_back(offset);
        Type* t = freeVarTypes_[i];
        if (byRef) {
            gcFreeVars_.push_back((int)offset);
            offset += 1;
        } else {
            u16 sw = (t && t->sizeWords_ > 0) ? (u16)t->sizeWords_ : (u16)1;
            if (sw == 1 && storesObjPtr(t)) {
                gcFreeVars_.push_back((int)offset);
            }
            offset += sw;
        }
    }
    totalFreeVarWords_ = offset;
}

VMString TemplateLambdaType::str() const {
    VMString s = rt::vmstr("fn<");
    for (size_t i = 0; i < typeParams_.size(); ++i) {
        if (i > 0) s += ", ";
        s += typeParams_[i];
    }
    s += ">(...) -> ...";
    return s;
}

LambdaType* TemplateLambdaType::findMono(const std::vector<Type*>& typeArgs) const {
    for (auto& entry : monoCache_) {
        if (entry.typeArgs.size() == typeArgs.size()) {
            bool match = true;
            for (size_t i = 0; i < typeArgs.size(); ++i) {
                if (entry.typeArgs[i] != typeArgs[i]) {
                    match = false;
                    break;
                }
            }
            if (match) return entry.lambdaType;
        }
    }
    return nullptr;
}

void TemplateLambdaType::addMono(std::vector<Type*> typeArgs, LambdaType* lt) {
    monoCache_.push_back({std::move(typeArgs), lt});
}

// MethodType constructor
MethodType::MethodType(TypeVec argTypes, Type* returnType, Type* receiverType)
    : FunctionType(std::move(argTypes), returnType)
    , receiverType_(receiverType)
{
    registerNewObj(this);
    classifyType(this);
}

// ============================================================================
// Runtime-storage predicate
// ============================================================================

bool storesObjPtr(Type const* t) {
    if (!t) return true;
    switch (t->repr_) {
        case Type::Repr::Atom:             return false;
        case Type::Repr::DiscriminantEnum: return false;
        case Type::Repr::UnwrappedTupleStruct: {
            // Recurse into the wrapped type via layout_[0].
            auto* st = static_cast<StructType const*>(t);
            if (!st->layout_.empty()) {
                return storesObjPtr(st->layout_[0].type);
            }
            return true;
        }
        case Type::Repr::Pointer:
        case Type::Repr::NullablePtrEnum:
        case Type::Repr::Inline:
        case Type::Repr::Heap:
            return true;
    }
    return true;
}

bool storesF64(Type const* t) {
    if (!t) return false;
    if (dynamic_cast<FloatType const*>(t)) return true;
    if (t->repr_ == Type::Repr::UnwrappedTupleStruct) {
        auto* st = static_cast<StructType const*>(t);
        if (!st->layout_.empty()) {
            return storesF64(st->layout_[0].type);
        }
    }
    return false;
}

// Phase 4e: array-element predicates. Match by Type* identity against the
// universe-cached Complex / Fraction types, recursing through aliases. Tuple
// structs that wrap a Complex/Fraction (UnwrappedTupleStruct) currently keep
// the boxed array path -- the wrapper gives a different concrete identity, so
// we'd need its layout_[0] to point at Complex/Fraction. Defer that.
bool isInlineComplexElem(Type const* t) {
    if (!t) return false;
    if (auto* al = dynamic_cast<AliasedType const*>(t)) {
        return isInlineComplexElem(al->aliasedType_);
    }
    return dynamic_cast<ComplexType const*>(t) != nullptr;
}

bool isInlineFractionElem(Type const* t) {
    if (!t) return false;
    if (auto* al = dynamic_cast<AliasedType const*>(t)) {
        return isInlineFractionElem(al->aliasedType_);
    }
    return dynamic_cast<FractionType const*>(t) != nullptr;
}

int nullablePtrVoidCaseIndex(EnumType const* et) {
    if (!et) return -1;
    for (size_t i = 0; i < et->cases_.size(); ++i) {
        Type* ct = et->cases_[i].type;
        if (ct && !ct->isObjType() && dynamic_cast<VoidType*>(ct)) {
            return (int)i;
        }
    }
    return -1;
}

// ============================================================================
// Phase 0 type classification
// ============================================================================

namespace {

// Set the simple "size-1 reference" classification for pointer-shaped types.
void setPointer(Type* t) {
    t->repr_ = Type::Repr::Pointer;
    t->sizeWords_ = 1;
    t->isValueType_ = true;   // value semantics: the reference itself is the value
}

// Mark a type as boxed/heap (default for user composites until proven otherwise).
void setHeap(Type* t) {
    t->repr_ = Type::Repr::Heap;
    t->sizeWords_ = 1;
    t->isValueType_ = false;
}

void classifyImpl(Type* t, std::unordered_set<Type*>& visiting);

// Phase 4g.1 eligibility helpers. A field/payload type counts as value-type-
// eligible for inline composition if its current Repr is one of the value
// reprs, OR it is itself a composite already flagged as couldBeInline_
// (transitive eligibility).
static bool isInlineEligibleField(Type const* t) {
    if (!t) return false;
    switch (t->repr_) {
        case Type::Repr::Atom:
        case Type::Repr::Pointer:
        case Type::Repr::DiscriminantEnum:
        case Type::Repr::NullablePtrEnum:
        case Type::Repr::UnwrappedTupleStruct:
        case Type::Repr::Inline:
            return true;
        case Type::Repr::Heap:
            // A Heap-repr value is stored at runtime as a single Obj* pointer,
            // exactly like Pointer-repr. So it is always inline-eligible as a
            // 1-word field -- inlineFootprintWords returns its sizeWords_ (1).
            // (couldBeInline_ stays relevant only for picking a flattened
            // multi-word footprint, which is handled by inlineFootprintWords.)
            return true;
    }
    return false;
}

// Footprint a field/payload would occupy in an inline composite layout.
// For atoms/pointers/etc this is sizeWords_. For composites already flagged
// couldBeInline_, the inline footprint takes precedence -- nested layouts
// compose using their would-be-inline sizes, not their current 1-word boxed
// runtime size.
static u8 inlineFootprintWords(Type const* t) {
    if (!t) return 1;
    if (t->couldBeInline_ && t->inlineLayoutWords_ > 0) {
        return t->inlineLayoutWords_;
    }
    return t->sizeWords_ > 0 ? t->sizeWords_ : 1;
}

// Plan defaults: ≤ 4 fields/cases, ≤ 4 words total inline footprint.
static constexpr unsigned kInlineMaxFields = 4;
static constexpr unsigned kInlineMaxWords  = 4;

void classifyStructImpl(StructType* st, std::unordered_set<Type*>& visiting) {
    st->layout_.clear();
    // Phase 4g.1: reset eligibility -- recomputed below.
    st->couldBeInline_     = false;
    st->inlineLayoutWords_ = 0;

    // Tuple-struct unwrap: 1-field tuple struct over a value type
    // collapses to the inner type's representation.
    //
    // Allowed inner reprs (Phase 1 + Phase 2):
    //   Pointer            -- slot holds an Obj* (Phase 1)
    //   Atom               -- slot holds Int/Float/Bool/Symbol (Phase 2)
    //   DiscriminantEnum   -- slot holds an i64 case index (Phase 2)
    // The runtime-storage audit (storesObjPtr/runtime dispatch updates) makes
    // these all safe to leak through containers, globals, ARC, etc.
    // Other reprs (Inline, NullablePtrEnum, Heap, recursive) keep boxing.
    if (st->isTupleStruct_ && st->fields_.size() == 1) {
        Type* inner = st->fields_[0].type;
        classifyImpl(inner, visiting);
        bool allowed = (inner->repr_ == Type::Repr::Pointer)
                    || (inner->repr_ == Type::Repr::Atom)
                    || (inner->repr_ == Type::Repr::DiscriminantEnum);
        if (allowed && !inner->isRecursive_) {
            st->repr_ = Type::Repr::UnwrappedTupleStruct;
            st->sizeWords_ = inner->sizeWords_;
            st->isValueType_ = true;
            st->layout_.push_back(FieldLayout{0, inner->sizeWords_, inner});
            return;
        }
        // Inline / NullablePtrEnum / recursive: fall through to normal handling.
    }

    // Phase 4c/4g.13: populate layout_ with one entry per field, using each
    // field's natural footprint (1 word for atoms/pointers, multi-word for
    // Inline composites like Fraction/Complex/Struct/Tuple). Heap-classified
    // structs now also use multi-word native storage for Inline composite
    // fields -- they are no longer recursively boxed at field-storage time.
    u32 wordOffset = 0;
    for (auto const& field : st->fields_) {
        if (field.type) classifyImpl(field.type, visiting);
        u8 fw = field.type ? (u8)inlineFootprintWords(field.type) : 1;
        st->layout_.push_back(FieldLayout{(u8)wordOffset, fw, field.type});
        wordOffset += fw;
    }
    setHeap(st);

    // Phase 4g.1: eligibility pre-flight for inline promotion. The layout
    // above is already multi-word for Inline composite fields, so the Inline
    // promotion path here only flips repr_/sizeWords_/isValueType_; the
    // layout itself is unchanged.
    if (!st->isRecursive_ && !st->fields_.empty()
        && st->fields_.size() <= kInlineMaxFields) {
        unsigned total = 0;
        bool ok = true;
        for (auto const& field : st->fields_) {
            if (!isInlineEligibleField(field.type)) { ok = false; break; }
            total += inlineFootprintWords(field.type);
        }
        if (ok && total <= kInlineMaxWords) {
            st->couldBeInline_     = true;
            st->inlineLayoutWords_ = (u8)total;
            st->repr_      = Type::Repr::Inline;
            st->sizeWords_ = (u8)total;
            st->isValueType_ = true;
        }
    }
}

void classifyTupleImpl(TupleType* tu, std::unordered_set<Type*>& visiting) {
    tu->layout_.clear();
    tu->couldBeInline_     = false;
    tu->inlineLayoutWords_ = 0;
    // Phase 4c/4g.13: populate layout_ with one entry per field, using each
    // field's natural footprint. See classifyStructImpl for details.
    u32 wordOffset = 0;
    for (Type* field : tu->fields_) {
        if (field) classifyImpl(field, visiting);
        u8 fw = field ? (u8)inlineFootprintWords(field) : 1;
        tu->layout_.push_back(FieldLayout{(u8)wordOffset, fw, field});
        wordOffset += fw;
    }
    setHeap(tu);

    if (!tu->isRecursive_ && !tu->fields_.empty()
        && tu->fields_.size() <= kInlineMaxFields) {
        unsigned total = 0;
        bool ok = true;
        for (Type* field : tu->fields_) {
            if (!isInlineEligibleField(field)) { ok = false; break; }
            total += inlineFootprintWords(field);
        }
        if (ok && total <= kInlineMaxWords) {
            tu->couldBeInline_     = true;
            tu->inlineLayoutWords_ = (u8)total;
            tu->repr_      = Type::Repr::Inline;
            tu->sizeWords_ = (u8)total;
            tu->isValueType_ = true;
        }
    }
}

void classifyEnumImpl(EnumType* en, std::unordered_set<Type*>& visiting) {
    en->layout_.clear();
    en->couldBeInline_     = false;
    en->inlineLayoutWords_ = 0;

    if (en->cases_.empty()) {
        setHeap(en);
        return;
    }

    // Classify each case payload first (unless Void).
    bool allVoid = true;
    int  voidCount = 0;
    int  pointerPayloadCount = 0;
    Type* nonVoidType = nullptr;
    for (auto const& c : en->cases_) {
        Type* pt = c.type;
        if (!pt) { setHeap(en); return; }
        bool isVoid = !pt->isObjType() && (dynamic_cast<VoidType*>(pt) != nullptr);
        if (isVoid) {
            ++voidCount;
            continue;
        }
        allVoid = false;
        nonVoidType = pt;
        classifyImpl(pt, visiting);
        if (pt->repr_ == Type::Repr::Pointer) {
            ++pointerPayloadCount;
        }
    }

    // DiscriminantEnum: all cases are Void -- represent as i64 tag.
    if (allVoid) {
        en->repr_ = Type::Repr::DiscriminantEnum;
        en->sizeWords_ = 1;
        en->isValueType_ = true;
        return;
    }

    // NullablePtrEnum: exactly 2 cases, one Void + one non-recursive Pointer payload.
    if (en->cases_.size() == 2 && voidCount == 1 && pointerPayloadCount == 1
        && nonVoidType && !nonVoidType->isRecursive_) {
        en->repr_ = Type::Repr::NullablePtrEnum;
        en->sizeWords_ = 1;
        en->isValueType_ = true;
        // Layout entry: payload at word 0 (the slot is just a nullable pointer)
        en->layout_.push_back(FieldLayout{0, 1, nonVoidType});
        return;
    }

    // Phase 4c/4g.15: populate layout_ with one entry per case payload,
    // indexed by case index. wordOffset is 0 because the heap Enum stores
    // the payload natively in its v[] flex array starting at v[0]; the
    // discriminant is held in the separate Enum::which_ field. sizeWords
    // is the actual payload footprint (1 for atoms/pointers, multi-word
    // for Inline composites like Fraction/Complex/sub-structs).
    en->layout_.clear();
    for (auto const& c : en->cases_) {
        Type* pt = c.type;
        bool isVoid = pt && !pt->isObjType()
                   && (dynamic_cast<VoidType*>(pt) != nullptr);
        u8 fw = isVoid ? 0 : (pt ? (u8)inlineFootprintWords(pt) : 1);
        en->layout_.push_back(FieldLayout{0, fw, pt});
    }
    setHeap(en);

    // Phase 4g.1: eligibility pre-flight. Inline enum footprint = 1 word
    // discriminant + max(case payload footprint). Each case payload is
    // either Void (counts as 0) or a value-type-eligible type.
    if (!en->isRecursive_ && !en->cases_.empty()
        && en->cases_.size() <= kInlineMaxFields) {
        unsigned maxPayload = 0;
        bool ok = true;
        for (auto const& c : en->cases_) {
            Type* pt = c.type;
            bool isVoid = pt && !pt->isObjType() && dynamic_cast<VoidType*>(pt);
            if (isVoid) continue;
            if (!isInlineEligibleField(pt)) { ok = false; break; }
            unsigned w = inlineFootprintWords(pt);
            if (w > maxPayload) maxPayload = w;
        }
        unsigned total = 1u + maxPayload;
        if (ok && total <= kInlineMaxWords) {
            en->couldBeInline_     = true;
            en->inlineLayoutWords_ = (u8)total;
            // Phase 4g.4: promote runtime classification. Slot layout:
            //   word 0       = i64 discriminant (case index)
            //   words 1..1+P = payload (P = caseSizeWords for the active case)
            // Unused tail words are not written. Each layout_[i] entry now
            // carries the per-case payload size with wordOffset = 1 (or 0
            // for Void cases; offset is meaningless since sizeWords = 0).
            // Box-at-boundary semantics mirror struct/tuple: a heap Enum*
            // is built by op_box_enum with a recursively-boxed word_ payload.
            en->repr_      = Type::Repr::Inline;
            en->sizeWords_ = (u8)total;
            en->isValueType_ = true;
            en->layout_.clear();
            for (auto const& c : en->cases_) {
                Type* pt = c.type;
                bool isVoid = pt && !pt->isObjType() && dynamic_cast<VoidType*>(pt);
                u8 fw = isVoid ? 0 : (u8)inlineFootprintWords(pt);
                u8 off = isVoid ? 0 : 1;
                en->layout_.push_back(FieldLayout{off, fw, pt});
            }
        }
    }
}

void classifyImpl(Type* t, std::unordered_set<Type*>& visiting) {
    if (!t) return;

    // Cycle guard: if we're already classifying t, mark recursive and return.
    if (!visiting.insert(t).second) {
        t->isRecursive_ = true;
        // A recursive type is always Heap-classified -- the NullablePtrEnum,
        // Inline, and DiscriminantEnum paths all refuse recursion, so the
        // final classification below will land on Heap regardless. Set it now
        // so a parent composite whose inline-eligibility check runs mid-cycle
        // sees the same single-Obj* footprint (1 word) this type ends up with,
        // instead of the default-but-unfinalized state.
        if (t->repr_ != Type::Repr::Heap) {
            t->repr_ = Type::Repr::Heap;
            t->sizeWords_ = 1;
        }
        return;
    }

    // Reset (overwriting any previous classification). isRecursive_ is preserved
    // if a descendant detected a cycle through us during this classification.
    t->isRecursive_ = false;

    if (auto* al = dynamic_cast<AliasedType*>(t)) {
        classifyImpl(al->aliasedType_, visiting);
        t->repr_ = al->aliasedType_->repr_;
        t->sizeWords_ = al->aliasedType_->sizeWords_;
        t->isValueType_ = al->aliasedType_->isValueType_;
        t->isRecursive_ = al->aliasedType_->isRecursive_;
        visiting.erase(t);
        return;
    }

    if (dynamic_cast<AtomType*>(t)) {
        t->repr_ = Type::Repr::Atom;
        t->sizeWords_ = 1;
        t->isValueType_ = true;
    }
    else if (auto* st = dynamic_cast<StructType*>(t)) {
        classifyStructImpl(st, visiting);
    }
    else if (auto* en = dynamic_cast<EnumType*>(t)) {
        classifyEnumImpl(en, visiting);
    }
    else if (auto* tu = dynamic_cast<TupleType*>(t)) {
        classifyTupleImpl(tu, visiting);
    }
    else if (t->isObjType()) {
        // Phase 4f: Complex is a 2-word inline value (real, imag) in registers,
        // boxed to a heap Complex* at storage boundaries. Same for Fraction.
        if (dynamic_cast<ComplexType*>(t)) {
            t->repr_ = Type::Repr::Inline;
            t->sizeWords_ = 2;
            t->isValueType_ = true;
        }
        else if (dynamic_cast<FractionType*>(t)) {
            t->repr_ = Type::Repr::Inline;
            t->sizeWords_ = 2;
            t->isValueType_ = true;
        }
        else {
            // String, Array, List, Range, Ref, Map, Set, Function, Lambda,
            // TemplateLambda, Coroutine, Any.
            // All represented as a single Obj* pointer; we do NOT recurse into
            // their element types (collections break cycles via heap allocation).
            setPointer(t);
        }
    }
    else {
        setHeap(t);
    }

    visiting.erase(t);
}

} // namespace

void classifyType(Type* t) {
    std::unordered_set<Type*> visiting;
    classifyImpl(t, visiting);
}

} // namespace ts
