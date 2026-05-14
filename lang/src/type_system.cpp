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
    , gcCases_(rt::STLAllocator<u8>(rt::gCurrentAllocator))
    , layout_(rt::STLAllocator<FieldLayout>(rt::gCurrentAllocator))
{
    registerNewObj(this);
    setCases(std::move(cases));
}

void EnumType::setCases(NameTypePairVec cases) {
    cases_ = std::move(cases);
    // Classify first so case-payload reprs are known, then mark gcCases_ based
    // on what actually lives in the payload slot at runtime (storesObjPtr),
    // not on the static isObjType() taxonomy.
    classifyType(this);
    gcCases_.clear();
    for (auto const& c : cases_) {
        gcCases_.push_back(storesObjPtr(c.type) ? 1 : 0);
    }
}

// StructType constructor
StructType::StructType(SymbolPtr name, NameTypePairVec fields, bool isTupleStruct)
    : name_(name)
    , gcFields_(rt::STLAllocator<int>(rt::gCurrentAllocator))
    , isTupleStruct_(isTupleStruct)
    , layout_(rt::STLAllocator<FieldLayout>(rt::gCurrentAllocator))
{
    registerNewObj(this);
    setFields(std::move(fields));
}

void StructType::setFields(NameTypePairVec fields) {
    fields_ = std::move(fields);
    classifyType(this);
    gcFields_.clear();
    int i = 0;
    for (auto const& field : fields_) {
        if (storesObjPtr(field.type)) {
            gcFields_.push_back(i);
        }
        ++i;
    }
}

// TupleType constructor
TupleType::TupleType(TypeVec fields)
    : fields_(std::move(fields))
    , gcFields_(rt::STLAllocator<int>(rt::gCurrentAllocator))
    , layout_(rt::STLAllocator<FieldLayout>(rt::gCurrentAllocator))
{
    registerNewObj(this);
    classifyType(this);
    int i = 0;
    for (Type* field : fields_) {
        if (field && storesObjPtr(field)) {
            gcFields_.push_back(i);
        }
        ++i;
    }
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

    // Inline value type: ≤ 4 fields, all fields are non-recursive value types,
    // total size ≤ 4 words.
    if (st->fields_.empty() || st->fields_.size() > 4) {
        setHeap(st);
        return;
    }

    u8 offset = 0;
    bool allInline = true;
    for (auto const& field : st->fields_) {
        if (!field.type) { allInline = false; break; }
        classifyImpl(field.type, visiting);
        if (!field.type->isValueType_ || field.type->isRecursive_) {
            allInline = false;
            break;
        }
        offset = static_cast<u8>(offset + field.type->sizeWords_);
        if (offset > 4) { allInline = false; break; }
    }

    if (!allInline) {
        setHeap(st);
        return;
    }

    // Build layout
    u8 cur = 0;
    for (auto const& field : st->fields_) {
        st->layout_.push_back(FieldLayout{cur, field.type->sizeWords_, field.type});
        cur = static_cast<u8>(cur + field.type->sizeWords_);
    }
    st->repr_ = Type::Repr::Inline;
    st->sizeWords_ = offset;
    st->isValueType_ = true;
}

void classifyTupleImpl(TupleType* tu, std::unordered_set<Type*>& visiting) {
    tu->layout_.clear();

    if (tu->fields_.empty() || tu->fields_.size() > 4) {
        setHeap(tu);
        return;
    }

    u8 offset = 0;
    bool allInline = true;
    for (Type* field : tu->fields_) {
        if (!field) { allInline = false; break; }
        classifyImpl(field, visiting);
        if (!field->isValueType_ || field->isRecursive_) {
            allInline = false;
            break;
        }
        offset = static_cast<u8>(offset + field->sizeWords_);
        if (offset > 4) { allInline = false; break; }
    }

    if (!allInline) {
        setHeap(tu);
        return;
    }

    u8 cur = 0;
    for (Type* field : tu->fields_) {
        tu->layout_.push_back(FieldLayout{cur, field->sizeWords_, field});
        cur = static_cast<u8>(cur + field->sizeWords_);
    }
    tu->repr_ = Type::Repr::Inline;
    tu->sizeWords_ = offset;
    tu->isValueType_ = true;
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

    // Inline enum: ≤ 4 cases, every payload is a non-recursive value type,
    // worst-case 1 + max(payload words) ≤ 4.
    if (en->cases_.size() > 4) {
        setHeap(en);
        return;
    }

    u8 maxPayload = 0;
    for (auto const& c : en->cases_) {
        Type* pt = c.type;
        bool isVoid = !pt->isObjType() && (dynamic_cast<VoidType*>(pt) != nullptr);
        if (isVoid) continue;
        if (!pt->isValueType_ || pt->isRecursive_) {
            setHeap(en);
            return;
        }
        if (pt->sizeWords_ > maxPayload) maxPayload = pt->sizeWords_;
    }
    u8 totalSize = static_cast<u8>(1 + maxPayload);
    if (totalSize > 4) {
        setHeap(en);
        return;
    }

    en->repr_ = Type::Repr::Inline;
    en->sizeWords_ = totalSize;
    en->isValueType_ = true;
    // One layout entry per case payload; payload always starts at word 1.
    for (auto const& c : en->cases_) {
        Type* pt = c.type;
        bool isVoid = !pt->isObjType() && (dynamic_cast<VoidType*>(pt) != nullptr);
        if (isVoid) {
            en->layout_.push_back(FieldLayout{1, 0, pt});
        } else {
            en->layout_.push_back(FieldLayout{1, pt->sizeWords_, pt});
        }
    }
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
        // String, Array, List, Range, Ref, Map, Set, Function, Lambda,
        // TemplateLambda, Coroutine, Any, Fraction, Complex.
        // All represented as a single Obj* pointer; we do NOT recurse into
        // their element types (collections break cycles via heap allocation).
        // Phase 4 will reclassify Fraction/Complex as Inline.
        setPointer(t);
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
