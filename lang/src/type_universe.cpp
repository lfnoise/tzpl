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
//  type_universe.cpp
//  lang
//
//  TypeUniverse implementation: system-allocated type interning
//

#include "type_universe.hpp"
#include "type_system.hpp"
#include "vm.hpp"

namespace ts {

// Forward declare the thread-local that TypeUniverse needs to manage
extern thread_local TypeUniverse* gCurrentTypeUniverse;

// RAII guard: sets the thread-local state needed for type creation,
// then restores all previous values. Type constructors read
// gCurrentTypeUniverse, and GCObj::operator new uses gCurrentAllocator
// (nullptr = system malloc, which is what we want for interned types).
struct TypeCreationScope {
    TypeUniverse* savedTU_;
    VM* savedVM_;
    rt::TLSFAllocator* savedAlloc_;
    explicit TypeCreationScope(TypeUniverse* self)
        : savedTU_(gCurrentTypeUniverse)
        , savedVM_(gCurrentVM)
        , savedAlloc_(rt::gCurrentAllocator)
    {
        gCurrentTypeUniverse = self;
        gCurrentVM = nullptr;
        rt::gCurrentAllocator = nullptr;
    }
    ~TypeCreationScope() {
        gCurrentTypeUniverse = savedTU_;
        gCurrentVM = savedVM_;
        rt::gCurrentAllocator = savedAlloc_;
    }
    TypeCreationScope(const TypeCreationScope&) = delete;
    TypeCreationScope& operator=(const TypeCreationScope&) = delete;
};

TypeUniverse::TypeUniverse() {
    // Ensure no VM allocator is active during type creation —
    // all types will be system-allocated (malloc).
    TypeCreationScope scope(this);

    // Bootstrap typeType: allocate raw memory, pre-set the pointer,
    // then placement-new so the constructor can find typeType.
    void* mem = ::malloc(sizeof(IntType));
    if (!mem) throw std::bad_alloc();

    types_.typeType = reinterpret_cast<Type*>(mem);
    new (mem) IntType();
    types_.typeType->type_ = types_.typeType;

    // Now allocate remaining built-in types
    types_.boolType = new BoolType();
    types_.intType = new IntType();
    types_.floatType = new FloatType();
    types_.symbolType = new SymbolType();
    types_.stringType = new StringType();
    types_.fractionType = new FractionType();
    types_.complexType = new ComplexType();
    types_.voidType = new VoidType();
    types_.anyType = new AnyType();

    // Unit type (empty tuple)
    types_.unitType = tupleType(nullptr, 0);

    if (!types_.boolType || !types_.intType || !types_.floatType || !types_.symbolType ||
        !types_.stringType || !types_.fractionType || !types_.complexType ||
        !types_.voidType || !types_.unitType) {
        throw std::bad_alloc();
    }

    // Pre-intern commonly used symbols
    syms_.some = intern("some");
    syms_.none = intern("none");

    // TypeCreationScope destructor restores all thread-locals
}

ArrayType* TypeUniverse::arrayType(Type* elemType) {
    auto it = arrayTypeCache_.find(elemType);
    if (it != arrayTypeCache_.end()) return it->second;
    TypeCreationScope scope(this);
    auto* t = new ArrayType(elemType);
    arrayTypeCache_[elemType] = t;
    return t;
}

ListType* TypeUniverse::listType(Type* elemType) {
    auto it = listTypeCache_.find(elemType);
    if (it != listTypeCache_.end()) return it->second;
    TypeCreationScope scope(this);
    auto* t = new ListType(elemType);
    listTypeCache_[elemType] = t;
    return t;
}

RangeType* TypeUniverse::rangeType(Type* elemType) {
    auto it = rangeTypeCache_.find(elemType);
    if (it != rangeTypeCache_.end()) return it->second;
    TypeCreationScope scope(this);
    auto* t = new RangeType(elemType);
    rangeTypeCache_[elemType] = t;
    return t;
}

RefType* TypeUniverse::refType(Type* elemType) {
    auto it = refTypeCache_.find(elemType);
    if (it != refTypeCache_.end()) return it->second;
    TypeCreationScope scope(this);
    auto* t = new RefType(elemType);
    refTypeCache_[elemType] = t;
    return t;
}

TupleType* TypeUniverse::tupleType(Type* const* fields, size_t count) {
    std::vector<Type*> key(fields, fields + count);
    auto it = tupleTypeCache_.find(key);
    if (it != tupleTypeCache_.end()) return it->second;

    TypeCreationScope scope(this);
    // Build the TypeVec for the TupleType constructor
    auto alloc = rt::STLAllocator<Type*>{rt::gCurrentAllocator};
    TypeVec tv{alloc};
    for (size_t i = 0; i < count; ++i) tv.push_back(fields[i]);
    auto* t = new TupleType(std::move(tv));
    tupleTypeCache_.emplace(std::move(key), t);
    return t;
}

MapType* TypeUniverse::mapType(Type* keyType, Type* valueType) {
    auto key = std::make_pair(keyType, valueType);
    auto it = mapTypeCache_.find(key);
    if (it != mapTypeCache_.end()) return it->second;
    TypeCreationScope scope(this);
    auto* t = new MapType(keyType, valueType);
    mapTypeCache_[key] = t;
    return t;
}

SetType* TypeUniverse::setType(Type* elemType) {
    auto it = setTypeCache_.find(elemType);
    if (it != setTypeCache_.end()) return it->second;
    TypeCreationScope scope(this);
    auto* t = new SetType(elemType);
    setTypeCache_[elemType] = t;
    return t;
}

EnumType* TypeUniverse::optionType(Type* elemType) {
    auto it = optionTypeCache_.find(elemType);
    if (it != optionTypeCache_.end()) return it->second;

    TypeCreationScope scope(this);
    // Build cases: [some(elemType), none(Void)]
    auto alloc = rt::STLAllocator<NameTypePair>{rt::gCurrentAllocator};
    NameTypePairVec cases{alloc};
    cases.push_back(NameTypePair{syms_.some, elemType});
    cases.push_back(NameTypePair{syms_.none, types_.voidType});

    // Build display name: "Option<ElemName>"
    auto elemStr = elemType->str();
    std::string displayName = "Option<";
    displayName += std::string(elemStr.data(), elemStr.size());
    displayName += ">";

    auto* enumType = new EnumType(intern(displayName), std::move(cases));
    optionTypeCache_[elemType] = enumType;
    return enumType;
}

CoroutineType* TypeUniverse::coroutineType(Type* yieldType) {
    auto it = coroutineTypeCache_.find(yieldType);
    if (it != coroutineTypeCache_.end()) return it->second;
    TypeCreationScope scope(this);
    auto* t = new CoroutineType(yieldType);
    coroutineTypeCache_[yieldType] = t;
    return t;
}

FunctionType* TypeUniverse::functionType(Type* const* argTypes, size_t argCount, Type* returnType) {
    // Build cache key: [argTypes..., returnType]
    std::vector<Type*> key;
    key.reserve(argCount + 1);
    for (size_t i = 0; i < argCount; ++i) key.push_back(argTypes[i]);
    key.push_back(returnType);

    auto it = functionTypeCache_.find(key);
    if (it != functionTypeCache_.end()) return it->second;

    TypeCreationScope scope(this);
    auto alloc = rt::STLAllocator<Type*>{rt::gCurrentAllocator};
    TypeVec tv{alloc};
    for (size_t i = 0; i < argCount; ++i) tv.push_back(argTypes[i]);
    auto* t = new FunctionType(std::move(tv), returnType);
    functionTypeCache_.emplace(std::move(key), t);
    return t;
}

} // namespace ts
