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

namespace ts {

// Type constructor
Type::Type() : Obj(gCurrentTypeUniverse->types().typeType) {}

// EnumType constructor
EnumType::EnumType(SymbolPtr name, NameTypePairVec cases)
    : name_(name)
    , cases_(std::move(cases))
    , gcCases_(rt::STLAllocator<u8>(rt::gCurrentAllocator))
{
    registerNewObj(this);
    for (auto const& c : cases_) {
        gcCases_.push_back(c.type->isObjType() ? 1 : 0);
    }
}

// StructType constructor
StructType::StructType(SymbolPtr name, NameTypePairVec fields, bool isTupleStruct)
    : name_(name)
    , fields_(std::move(fields))
    , gcFields_(rt::STLAllocator<int>(rt::gCurrentAllocator))
    , isTupleStruct_(isTupleStruct)
{
    registerNewObj(this);
    int i = 0;
    for (auto const& field : fields_) {
        if (field.type->isObjType()) {
            gcFields_.push_back(i);
        }
        ++i;
    }
}

// TupleType constructor
TupleType::TupleType(TypeVec fields)
    : fields_(std::move(fields))
    , gcFields_(rt::STLAllocator<int>(rt::gCurrentAllocator))
{
    registerNewObj(this);
    int i = 0;
    for (Type* field : fields_) {
        if (field->isObjType()) {
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
}

// LambdaType constructor
LambdaType::LambdaType(TypeVec argTypes, Type* returnType, TypeVec freeVarTypes)
    : FunctionType(std::move(argTypes), returnType)
    , freeVarTypes_(std::move(freeVarTypes))
    , gcFreeVars_(rt::STLAllocator<int>(rt::gCurrentAllocator))
{
    registerNewObj(this);
    int i = 0;
    for (Type* t : freeVarTypes_) {
        if (t->isObjType()) {
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
    int i = 0;
    for (Type* t : freeVarTypes_) {
        if (t->isObjType()) {
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
}

} // namespace ts
