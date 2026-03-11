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

// Base Type class
class Type : public Obj {
public:
    Type();
    Type(Type* typeType) : Obj(typeType) {}

    virtual bool isObjType() const = 0;

    // Type objects are permanent (system-allocated, never GC'd).
    // They only reference other permanent types, so scanning is a no-op.
    void gcScan(GC* gc, i32& ioWordsToScan) override {}
};

using TypeVec = Vec<Type*>;

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
    Vec<u8> gcCases_;

    EnumType(SymbolPtr name, NameTypePairVec cases);

    void setCases(NameTypePairVec cases) {
        cases_ = std::move(cases);
        gcCases_.clear();
        for (auto const& c : cases_) {
            gcCases_.push_back(c.type->isObjType() ? 1 : 0);
        }
    }

    VMString str() const override { return rt::vmstr(name_->str()); }

};

// Struct (product type)
class StructType : public ObjType {
public:
    SymbolPtr name_;
    NameTypePairVec fields_;
    Vec<int> gcFields_;
    bool isTupleStruct_ = false;

    StructType(SymbolPtr name, NameTypePairVec fields, bool isTupleStruct = false);

    void setFields(NameTypePairVec fields) {
        fields_ = std::move(fields);
        gcFields_.clear();
        int i = 0;
        for (auto const& field : fields_) {
            if (field.type->isObjType()) {
                gcFields_.push_back(i);
            }
            ++i;
        }
    }

    VMString str() const override { return rt::vmstr(name_->str()); }

};

// Tuple type
class TupleType : public ObjType {
public:
    TypeVec fields_;
    Vec<int> gcFields_;

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
    Vec<int> gcFreeVars_;
    CodeBlock* codeBlock_ = nullptr;   // compiled body for this lambda

    LambdaType(TypeVec argTypes, Type* returnType, TypeVec freeVarTypes);

};

// Template lambda type — a generic lambda awaiting monomorphization
class TemplateLambdaType : public ObjType {
public:
    LambdaExprNode* astNode_;                     // for re-checking per instantiation
    TypeVec freeVarTypes_;                         // capture types (always concrete)
    Vec<int> gcFreeVars_;                          // GC indices for obj-typed captures
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

// Any type — wraps a value of any type into a uniform object
class AnyType : public ObjType {
public:
    AnyType() { registerNewObj(this); }
    VMString str() const override { return rt::vmstr("Any"); }
};

// Method type
class MethodType : public FunctionType {
public:
    Type* receiverType_;

    MethodType(TypeVec argTypes, Type* returnType, Type* receiverType);

};

} // namespace ts

#endif /* type_system_hpp */
