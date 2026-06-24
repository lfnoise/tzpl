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
//  type_universe.hpp
//  lang
//
//  Global type interning: system-allocated, shared across VMs.
//  Types are immortal (never GC'd) and pointer-stable once created.
//

#ifndef type_universe_hpp
#define type_universe_hpp

#include "symbol.hpp"
#include <unordered_map>
#include <vector>

namespace ts {

// Forward declarations
class Type;
class ArrayType;
class ListType;
class RangeType;
class RefType;
class MapType;
class SetType;
class PersistentVectorType;
class PersistentMapType;
class TupleType;
class FunctionType;
class EnumType;
class CoroutineType;
class FutureType;

// Built-in type pointers
struct BuiltinTypes {
    Type* typeType = nullptr;
    Type* boolType = nullptr;
    Type* intType = nullptr;
    Type* floatType = nullptr;
    Type* symbolType = nullptr;
    Type* stringType = nullptr;
    Type* fractionType = nullptr;
    Type* complexType = nullptr;
    Type* voidType = nullptr;
    Type* anyType = nullptr;
    TupleType* unitType = nullptr;
};

// Pre-interned symbols for hot paths (avoids intern() during execution)
struct BuiltinSymbols {
    SymbolPtr some = nullptr;   // "some"
    SymbolPtr none = nullptr;   // "none"
};

// Hash for std::vector<Type*> keys in type caches
struct TypeVecHash {
    size_t operator()(const std::vector<Type*>& v) const noexcept {
        size_t h = v.size();
        for (const auto* t : v) {
            h ^= std::hash<const void*>{}(t) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

// Hash for (Type*, Type*) pairs in map type cache
struct TypePairHash {
    size_t operator()(const std::pair<Type*, Type*>& p) const noexcept {
        size_t h = std::hash<const void*>{}(p.first);
        h ^= std::hash<const void*>{}(p.second) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// TypeUniverse: owns all interned types, system-allocated (malloc), never GC'd.
// Shared across VMs and compilers via reference.
class TypeUniverse {
public:
    TypeUniverse();

    // Non-copyable, non-movable
    TypeUniverse(const TypeUniverse&) = delete;
    TypeUniverse& operator=(const TypeUniverse&) = delete;

    // Accessors
    const BuiltinTypes& types() const { return types_; }
    const BuiltinSymbols& syms() const { return syms_; }

    // Interned composite types.
    // tupleType/functionType accept any contiguous container of Type* (Vec<Type*>,
    // std::vector<Type*>, etc.) — just need .data() and .size().
    ArrayType* arrayType(Type* elemType);
    ListType* listType(Type* elemType);
    RangeType* rangeType(Type* elemType);
    RefType* refType(Type* elemType);
    TupleType* tupleType(Type* const* fields, size_t count);
    FunctionType* functionType(Type* const* argTypes, size_t argCount, Type* returnType);
    MapType* mapType(Type* keyType, Type* valueType);
    SetType* setType(Type* elemType);
    PersistentVectorType* persistentVectorType(Type* elemType);
    PersistentMapType* persistentMapType(Type* keyType, Type* valueType);
    EnumType* optionType(Type* elemType);
    CoroutineType* coroutineType(Type* yieldType);
    FutureType* futureType(Type* valueType);

    // Convenience overloads accepting any container with .data() and .size()
    template <typename Container>
    TupleType* tupleType(const Container& fields) {
        return tupleType(fields.data(), fields.size());
    }

    template <typename Container>
    FunctionType* functionType(const Container& argTypes, Type* returnType) {
        return functionType(argTypes.data(), argTypes.size(), returnType);
    }

private:
    BuiltinTypes types_;
    BuiltinSymbols syms_;

    // Type interning caches (system-allocated std containers)
    std::unordered_map<Type*, ArrayType*> arrayTypeCache_;
    std::unordered_map<Type*, ListType*> listTypeCache_;
    std::unordered_map<Type*, RangeType*> rangeTypeCache_;
    std::unordered_map<Type*, RefType*> refTypeCache_;
    std::unordered_map<std::vector<Type*>, TupleType*, TypeVecHash> tupleTypeCache_;
    std::unordered_map<std::vector<Type*>, FunctionType*, TypeVecHash> functionTypeCache_;
    std::unordered_map<std::pair<Type*, Type*>, MapType*, TypePairHash> mapTypeCache_;
    std::unordered_map<Type*, SetType*> setTypeCache_;
    std::unordered_map<Type*, PersistentVectorType*> persistentVectorTypeCache_;
    std::unordered_map<std::pair<Type*, Type*>, PersistentMapType*, TypePairHash> persistentMapTypeCache_;
    std::unordered_map<Type*, EnumType*> optionTypeCache_;
    std::unordered_map<Type*, CoroutineType*> coroutineTypeCache_;
    std::unordered_map<Type*, FutureType*> futureTypeCache_;
};

} // namespace ts

#endif /* type_universe_hpp */
