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
{
    registerNewObj(this);
    classifyType(this);
    int i = 0;
    for (Type* t : freeVarTypes_) {
        if (storesObjPtr(t)) {
            gcFreeVars_.push_back(i);
        }
        ++i;
    }
}

// TemplateLambdaType constructor
TemplateLambdaType::TemplateLambdaType(LambdaExprNode* node, TypeVec freeVarTypes,
                                       std::vector<std::string> typeParams,
                                       std::vector<ConstraintEntry> constraints)
    : astNode_(node)
    , freeVarTypes_(std::move(freeVarTypes))
    , gcFreeVars_(rt::STLAllocator<int>(rt::gCurrentAllocator))
    , typeParams_(std::move(typeParams))
    , constraints_(std::move(constraints))
{
    registerNewObj(this);
    classifyType(this);
    int i = 0;
    for (Type* t : freeVarTypes_) {
        if (storesObjPtr(t)) {
            gcFreeVars_.push_back(i);
        }
        ++i;
    }
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

void classifyStructImpl(StructType* st, std::unordered_set<Type*>& visiting) {
    st->layout_.clear();

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

    // Phase 4f: struct runtime is still heap (Struct*); the inline machinery
    // only supports Complex / Fraction so far. Defer struct inlining until
    // struct codegen / containers are wired up.
    //
    // Phase 4c: populate layout_ with one entry per field. Word offsets
    // currently equal field indices because every field still occupies one
    // Word in Struct::v[]. The runtime walker (Struct::releaseChildren,
    // op_make_struct retain pass) uses layout_ to find Obj* fields.
    u8 wordOffset = 0;
    for (auto const& field : st->fields_) {
        if (field.type) classifyImpl(field.type, visiting);
        st->layout_.push_back(FieldLayout{wordOffset, 1, field.type});
        ++wordOffset;
    }
    setHeap(st);
}

void classifyTupleImpl(TupleType* tu, std::unordered_set<Type*>& visiting) {
    tu->layout_.clear();
    // Phase 4f: tuple runtime is still heap (Tuple*); the inline machinery
    // only supports Complex / Fraction so far. Defer tuple inlining until
    // tuple codegen / containers are wired up.
    //
    // Phase 4c: populate layout_ with one entry per field. Word offsets
    // currently equal field indices because every field still occupies one
    // Word in Tuple::v[].
    u8 wordOffset = 0;
    for (Type* field : tu->fields_) {
        if (field) classifyImpl(field, visiting);
        tu->layout_.push_back(FieldLayout{wordOffset, 1, field});
        ++wordOffset;
    }
    setHeap(tu);
}

void classifyEnumImpl(EnumType* en, std::unordered_set<Type*>& visiting) {
    en->layout_.clear();

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

    // Phase 4f: enum runtime is still heap (Enum*); the inline machinery
    // only supports Complex / Fraction so far. Defer enum inlining.
    //
    // Phase 4c: populate layout_ with one entry per case payload, indexed by
    // case index. wordOffset is 0 because the heap Enum has a single
    // dedicated payload slot (Enum::word_); the offset is meaningful only
    // once enums become inline. Type carries the case payload type --
    // VoidType for no-data cases. Walkers select layout_[which_] then check
    // storesObjPtr() on the type to decide whether to retain/release.
    en->layout_.clear();
    for (auto const& c : en->cases_) {
        en->layout_.push_back(FieldLayout{0, 1, c.type});
    }
    setHeap(en);
}

void classifyImpl(Type* t, std::unordered_set<Type*>& visiting) {
    if (!t) return;

    // Cycle guard: if we're already classifying t, mark recursive and return.
    if (!visiting.insert(t).second) {
        t->isRecursive_ = true;
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
