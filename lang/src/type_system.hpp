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
//  type_system.hpp
//  tiny-static-2
//
//  Type system for statically-typed real-time interpreter
//

#ifndef type_system_hpp
#define type_system_hpp

#include "vm.hpp"

namespace ts {

class CodeBlock;  // forward declaration
struct LambdaExprNode;  // forward declaration from ast.hpp
class Type;

// Phase 0 layout entry: one per struct/tuple field or one per enum case payload.
// Word offsets are within the inline storage of the parent type.
struct FieldLayout {
    u8    wordOffset;   // first word of this field/payload within the parent
    u8    sizeWords;    // 1..4 for inline value types; 1 for atoms/pointers
    Type* type;         // child type (already classified)
};

// Base Type class
class Type : public Obj {
public:
    // Phase 0 representation classification.
    // Filled in by classifyType(); see plan
    // (.claude/plans/i-would-like-you-flickering-nova.md) for rules.
    enum class Repr : u8 {
        Atom,                  // Bool/Int/Float/Symbol/Void -- 1 word, not a pointer
        Pointer,               // Obj* reference (String, Array, Map, Fraction, ...) -- 1 word
        DiscriminantEnum,      // Enum with all-Void cases -- value is i64 tag
        NullablePtrEnum,       // 2-case enum (Void + single Pointer payload) -- nullable Obj*
        UnwrappedTupleStruct,  // 1-field tuple struct over a value type -- carries inner repr
        Inline,                // multi-word inline struct/tuple/enum (Phase 4)
        Heap                   // boxed via Obj* (current default for user composites)
    };

    Repr repr_         = Repr::Heap;
    u8   sizeWords_    = 1;
    bool isValueType_  = false;
    bool isRecursive_  = false;

    // Phase 4g.1: descriptive metadata for composites that are eligible for
    // inline value-type runtime, computed by classifyType but NOT yet acted
    // on by codegen/runtime. Future phases (4g.2+) flip repr_ to Inline and
    // sizeWords_ to inlineLayoutWords_ once construction/access opcodes are
    // wired to read/write multi-word slots for the type.
    //
    // Eligibility (struct/tuple/enum): non-recursive AND fields-or-cases <= 4
    // AND every field/payload classifies as a value type (Atom, Pointer,
    // DiscriminantEnum, NullablePtrEnum, UnwrappedTupleStruct, Inline) AND
    // total slot footprint <= 4 words. Enums add a discriminant word.
    bool couldBeInline_     = false;
    u8   inlineLayoutWords_ = 0;     // total inline footprint when promoted

    Type();
    Type(Type* typeType) : Obj(typeType) {}

    virtual bool isObjType() const = 0;
};

using TypeVec = Vec<Type*>;

// Compute repr_/sizeWords_/isValueType_/isRecursive_ for `t` and (for struct/tuple/enum)
// the layout_ vector. Recurses through struct fields, tuple fields, and enum case payloads;
// pointer/array/map/etc. types are size-1 references and do not recurse into their
// element types (which breaks structural cycles via collections). Safe to call multiple
// times -- each call overwrites the previous classification.
void classifyType(Type* t);

// Phase 0+ runtime-storage predicate: does a Word holding a value of this type
// contain an Obj* pointer at runtime? Distinct from isObjType(), which describes
// the static type taxonomy. ARC, GC field marking, AnyObj boxing, and container
// element dispatch should all use this -- isObjType() is reserved for type-system
// logic (compatibility, generic constraints, etc.).
//
//   Atom, DiscriminantEnum             -> false
//   Pointer, NullablePtrEnum, Inline
//   (still boxed in Phase 1-3), Heap   -> true
//   UnwrappedTupleStruct               -> delegates to its inner type (layout_[0])
bool storesObjPtr(Type const* t);

// Like storesObjPtr, but for UNBOXED storage (registers, globals, dyn vars)
// where a 1-word inline composite is stored as its single field's raw word --
// so we must recurse into that field rather than treat the word as an Obj*
// (storesObjPtr returns true for ALL Repr::Inline, which is correct only for
// boxed slots). Multi-word inline values aren't traced per-word, so false.
// This is the predicate GC roots must use; storesObjPtr over-approximates for
// inline composites and makes the marker dereference e.g. a Float or enum tag.
bool storesObjPtrUnboxed(Type const* t);

// True iff a multi-word INLINE composite (struct/tuple/enum) embeds at least one
// traceable Obj* field anywhere in its layout. Used to decide whether a register
// holding such a value needs a GC stack-map entry (gcScanPayload walk).
bool inlineHasObjPtr(Type const* t);

// True iff a value of this type is stored as f64 at runtime. Used for
// PodArray<i64> / PodArray<f64> / ObjArray dispatch. UnwrappedTupleStruct
// delegates to its inner type.
bool storesF64(Type const* t);

// Phase 4e: array element classification beyond the original 3-way dispatch.
// For Array[Complex] / Array[Fraction] we want inline 16-byte slots
// (PodArray<x64> / PodArray<r64>) instead of one boxed Obj* per element.
// Both predicates are false for everything else, so the existing
// Pod/Obj dispatch falls through unchanged.
bool isInlineComplexElem(Type const* t);
bool isInlineFractionElem(Type const* t);

// For a NullablePtrEnum, returns the case index whose payload is Void
// (the "none" side). The other case index is the data ("some") side.
// Returns -1 if no Void case found.
int nullablePtrVoidCaseIndex(EnumType const* et);

// Type alias
class AliasedType : public Type {
public:
    SymbolPtr name_;
    Type* aliasedType_;

    AliasedType(SymbolPtr name, Type* type)
        : name_(name), aliasedType_(type)
    {
        registerNewObj(this);
    }

    VMString str() const override { return rt::vmstr(name_->str()); }
    bool isObjType() const override { return type_->isObjType(); }

};

// Atom types (stored in 64-bit word)
class AtomType : public Type {
public:
    AtomType() {
        registerNewObj(this);
    }
    bool isObjType() const override { return false; }
};

class BoolType : public AtomType {
public:
    VMString str() const override { return rt::vmstr("Bool"); }
};

class VoidType : public AtomType {
public:
    VMString str() const override { return rt::vmstr("Void"); }
};

class IntType : public AtomType {
public:
    VMString str() const override { return rt::vmstr("Int"); }
};

class FloatType : public AtomType {
public:
    VMString str() const override { return rt::vmstr("Float"); }
};

class SymbolType : public AtomType {
public:
    VMString str() const override { return rt::vmstr("Symbol"); }
};

// Object types (stored by pointer)
class ObjType : public Type {
public:
    bool isObjType() const override { return true; }
};

class FractionType : public ObjType {
public:
    FractionType() {
        registerNewObj(this);
    }
    VMString str() const override { return rt::vmstr("Fraction"); }
};

class ComplexType : public ObjType {
public:
    ComplexType() {
        registerNewObj(this);
    }
    VMString str() const override { return rt::vmstr("Complex"); }
};

class StringType : public ObjType {
public:
    StringType() {
        registerNewObj(this);
    }
    VMString str() const override { return rt::vmstr("String"); }
};

// Bytes: a length-based binary buffer (Repr::Pointer, like String). Holds
// arbitrary bytes including embedded NULs; no UTF-8 assumptions.
class BytesType : public ObjType {
public:
    BytesType() {
        registerNewObj(this);
    }
    VMString str() const override { return rt::vmstr("Bytes"); }
};

// Array type
class ArrayType : public ObjType {
public:
    Type* elemType_;

    ArrayType(Type* elemType)
        : elemType_(elemType)
    {
        registerNewObj(this);
    }

    VMString str() const override {
        return rt::fmt("[{}]", elemType_->str());
    }

};

// List type (singly-linked immutable list)
class ListType : public ObjType {
public:
    Type* elemType_;

    ListType(Type* elemType)
        : elemType_(elemType)
    {
        registerNewObj(this);
    }

    VMString str() const override {
        return rt::fmt("List<{}>", elemType_->str());
    }

};

// Range type
class RangeType : public ObjType {
public:
    Type* elemType_;

    RangeType(Type* elemType)
        : elemType_(elemType)
    {
        registerNewObj(this);
    }

    VMString str() const override {
        return rt::fmt("Range<{}>", elemType_->str());
    }

};

// Ref type (mutable reference)
class RefType : public ObjType {
public:
    Type* elemType_;

    RefType(Type* elemType)
        : elemType_(elemType)
    {
        registerNewObj(this);
    }

    VMString str() const override {
        return rt::fmt("Ref<{}>", elemType_->str());
    }

};

// Map type
class MapType : public ObjType {
public:
    Type* keyType_;
    Type* valueType_;

    MapType(Type* keyType, Type* valueType)
        : keyType_(keyType), valueType_(valueType)
    {
        registerNewObj(this);
    }

    VMString str() const override {
        return rt::fmt("[{}:{}]", keyType_->str(), valueType_->str());
    }

};

// Set type
class SetType : public ObjType {
public:
    Type* elemType_;

    SetType(Type* elemType)
        : elemType_(elemType)
    {
        registerNewObj(this);
    }

    VMString str() const override {
        return rt::fmt("Set<{}>", elemType_->str());
    }

};

// Persistent vector type (immutable, array-mapped-trie backed): #[T]
class PersistentVectorType : public ObjType {
public:
    Type* elemType_;

    PersistentVectorType(Type* elemType)
        : elemType_(elemType)
    {
        registerNewObj(this);
    }

    VMString str() const override {
        return rt::fmt("#[{}]", elemType_->str());
    }

};

// Persistent map type (immutable, HAMT backed): #[K:V]
class PersistentMapType : public ObjType {
public:
    Type* keyType_;
    Type* valueType_;

    PersistentMapType(Type* keyType, Type* valueType)
        : keyType_(keyType), valueType_(valueType)
    {
        registerNewObj(this);
    }

    VMString str() const override {
        return rt::fmt("#[{}:{}]", keyType_->str(), valueType_->str());
    }

};

// Struct/Enum field
struct NameTypePair {
    SymbolPtr name;
    Type* type;
};

using NameTypePairVec = Vec<NameTypePair>;

// Enum (sum type)
class EnumType : public ObjType {
public:
    SymbolPtr name_;
    NameTypePairVec cases_;
    Vec<FieldLayout> layout_;   // one entry per case payload (Phase 0/4c)
    // Lazily-filled immortal singletons for no-data (Void-payload) cases of a
    // Heap-repr enum. Indexed by case index; nullptr until first construction.
    // Lets every reference to e.g. `Tree.leaf` share one heap object instead
    // of allocating a fresh Enum per use.
    Vec<Obj*> noDataSingletons_;

    EnumType(SymbolPtr name, NameTypePairVec cases);

    void setCases(NameTypePairVec cases);

    VMString str() const override { return rt::vmstr(name_->str()); }

};

// Struct (product type)
class StructType : public ObjType {
public:
    SymbolPtr name_;
    NameTypePairVec fields_;
    bool isTupleStruct_ = false;
    Vec<FieldLayout> layout_;   // one entry per field (Phase 0/4c)

    StructType(SymbolPtr name, NameTypePairVec fields, bool isTupleStruct = false);

    void setFields(NameTypePairVec fields);

    VMString str() const override { return rt::vmstr(name_->str()); }

};

// Tuple type
class TupleType : public ObjType {
public:
    TypeVec fields_;
    Vec<int> gcFields_;
    Vec<FieldLayout> layout_;   // one entry per field (Phase 0)

    TupleType(TypeVec fields);

    VMString str() const override {
        VMString s = rt::vmstr("(");
        bool once = false;
        for (Type* type : fields_) {
            if (once) s += ", ";
            once = true;
            s += type->str();
        }
        if (fields_.size() == 1) s += ",";
        s += ")";
        return s;
    }

};

// Function type
class FunctionType : public ObjType {
public:
    TypeVec argTypes_;
    Type* returnType_;

    FunctionType(TypeVec argTypes, Type* returnType);

    VMString str() const override {
        VMString s = rt::vmstr("fn(");
        for (size_t i = 0; i < argTypes_.size(); ++i) {
            if (i > 0) s += ", ";
            s += argTypes_[i]->str();
        }
        s += ") ";
        s += returnType_->str();
        return s;
    }

};

// Lambda (closure) type
class LambdaType : public FunctionType {
public:
    TypeVec freeVarTypes_;
    Vec<int> gcFreeVars_;            // word offsets in freeVars_ that hold Obj*
    // Per-capture layout. Indexed by capture number, parallel to freeVarTypes_.
    // - freeVarIsUpvar_[i]: true if capture i is byReference (UpVar*, 1 word).
    // - freeVarOffsets_[i]: starting word offset of capture i within the
    //   lambda's freeVars_ block. Width is 1 for upvar captures, otherwise
    //   the captured type's sizeWords_.
    // - totalFreeVarWords_: sum of all capture widths (== Lambda.numFreeVars_).
    Vec<u8>  freeVarIsUpvar_;
    Vec<u16> freeVarOffsets_;
    u16      totalFreeVarWords_ = 0;
    CodeBlock* codeBlock_ = nullptr;   // compiled body for this lambda

    LambdaType(TypeVec argTypes, Type* returnType, TypeVec freeVarTypes);

    // Phase: upvars. Re-derive freeVarOffsets_, totalFreeVarWords_, and
    // gcFreeVars_ now that we know which captures are byReference (UpVar*,
    // 1 word) vs byValue (sizeWords words). When called with an empty
    // isUpvar vector, treats every capture as byValue and matches the
    // legacy single-word-per-capture layout exactly.
    void setCaptureLayout(const std::vector<bool>& isUpvar);
};

// Template lambda type — a generic lambda awaiting monomorphization
class TemplateLambdaType : public ObjType {
public:
    LambdaExprNode* astNode_;                     // for re-checking per instantiation
    TypeVec freeVarTypes_;                         // capture types (always concrete)
    Vec<int> gcFreeVars_;                          // word offsets in freeVars_ that hold Obj*
    Vec<u8>  freeVarIsUpvar_;                      // parallel to freeVarTypes_
    Vec<u16> freeVarOffsets_;                      // start word of each capture in freeVars_
    u16      totalFreeVarWords_ = 0;
    std::vector<std::string> typeParams_;           // ["T", "U"]

    // Constraint info stored as pairs to avoid ast.hpp dependency
    struct ConstraintEntry {
        std::string typeParam;
        std::string constraintName;
    };
    std::vector<ConstraintEntry> constraints_;

    // Mono cache: maps type args -> concrete LambdaType* (each with its own CodeBlock)
    struct MonoEntry {
        std::vector<Type*> typeArgs;
        LambdaType* lambdaType;
    };
    std::vector<MonoEntry> monoCache_;

    TemplateLambdaType(LambdaExprNode* node, TypeVec freeVarTypes,
                       std::vector<std::string> typeParams,
                       std::vector<ConstraintEntry> constraints);

    // See LambdaType::setCaptureLayout. Mirrors the same offset / GC mask
    // computation onto the template-side LambdaType so the Lambda objects
    // built via op_make_template_lambda trace their UpVar captures.
    void setCaptureLayout(const std::vector<bool>& isUpvar);

    bool isObjType() const override { return true; }

    LambdaType* findMono(const std::vector<Type*>& typeArgs) const;
    void addMono(std::vector<Type*> typeArgs, LambdaType* lt);

    VMString str() const override;

};

// Coroutine type
class CoroutineType : public ObjType {
public:
    Type* yieldType_;

    CoroutineType(Type* yieldType)
        : yieldType_(yieldType)
    {
        registerNewObj(this);
    }

    VMString str() const override {
        return rt::fmt("Coroutine<{}>", yieldType_->str());
    }

};

// Future type — the result of an asynchronous (`async fn`) computation.
// Modeled on CoroutineType: a single-type-parameter heap object. `await`
// unwraps Future<T> -> T.
class FutureType : public ObjType {
public:
    Type* valueType_;

    FutureType(Type* valueType)
        : valueType_(valueType)
    {
        registerNewObj(this);
    }

    VMString str() const override {
        return rt::fmt("Future<{}>", valueType_->str());
    }

};

// Actor type — a lightweight actor whose mailbox carries messages of type M.
// Modeled on CoroutineType/FutureType: a single-type-parameter heap object.
// `receive(Actor<M>)` yields `Future<M>`; `send(Actor<M>, M)`.
class ActorType : public ObjType {
public:
    Type* msgType_;

    ActorType(Type* msgType)
        : msgType_(msgType)
    {
        registerNewObj(this);
    }

    VMString str() const override {
        return rt::fmt("Actor<{}>", msgType_->str());
    }

};

// Any type — wraps a value of any type into a uniform object
class AnyType : public ObjType {
public:
    AnyType() { registerNewObj(this); }
    VMString str() const override { return rt::vmstr("Any"); }
};

// Existential type — `some C`: a value of some statically-hidden type that
// satisfies the structural constraint named by constraintName_, erased behind a
// witness dictionary. Represented at runtime as a single Obj* (Repr::Pointer).
// Phase 1: the type exists and type-checks; packing and dispatch arrive in
// later phases.
class ExistentialType : public ObjType {
public:
    std::string constraintName_;

    explicit ExistentialType(std::string constraintName)
        : constraintName_(std::move(constraintName))
    {
        registerNewObj(this);
    }

    VMString str() const override {
        return rt::fmt("some {}", constraintName_);
    }
};

// Method type
class MethodType : public FunctionType {
public:
    Type* receiverType_;

    MethodType(TypeVec argTypes, Type* returnType, Type* receiverType);

};

} // namespace ts

#endif /* type_system_hpp */
