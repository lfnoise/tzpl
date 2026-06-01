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
//  type_checker_types.cpp
//  lang
//
//  Type checker -- type expression resolution and monomorphization
//

#include "type_checker.hpp"
#include "module_compiler.hpp"
#include "builtins.hpp"
#include "value.hpp"
#include "diagnostic.hpp"

namespace ts {

// Intern the `some C` existential type for a constraint, creating and
// classifying it on first use. Callers must have already validated that the
// constraint exists and is object-safe (see resolveTypeExpr).
ExistentialType* TypeChecker::existentialTypeFor(const std::string& constraintName) {
    auto it = existentialTypes_.find(constraintName);
    if (it != existentialTypes_.end()) return it->second;
    auto* et = new ExistentialType(constraintName);
    classifyType(et);   // ObjType -> Repr::Pointer (single Obj*)
    existentialTypes_[constraintName] = et;
    return et;
}

// --- Resolve type expressions ---

Type* TypeChecker::resolveTypeExpr(TypeExpr* typeExpr) {
    if (!typeExpr) return nullptr;

    if (typeExpr->kind == ASTNode::NamedType) {
        auto* named = static_cast<NamedTypeNode*>(typeExpr);
        // Check active type parameter bindings (for template monomorphization)
        auto tpIt = typeParamBindings_.find(named->name);
        if (tpIt != typeParamBindings_.end()) return tpIt->second;
        if (named->name == "Int") return compiler_.intType();
        if (named->name == "Float") return compiler_.floatType();
        if (named->name == "String") return compiler_.stringType();
        if (named->name == "Bool") return compiler_.boolType();
        if (named->name == "Symbol") return compiler_.symbolType();
        if (named->name == "Void") return compiler_.voidType();
        if (named->name == "Fraction") return compiler_.fractionType();
        if (named->name == "Complex") return compiler_.complexType();
        if (named->name == "Any") return compiler_.anyType();
        // Check struct types
        auto it = structTypes_.find(named->name);
        if (it != structTypes_.end()) return it->second;
        // Check enum types
        auto eit = enumTypes_.find(named->name);
        if (eit != enumTypes_.end()) return eit->second;
        // Check concrete type aliases
        auto ait = typeAliases_.find(named->name);
        if (ait != typeAliases_.end()) return ait->second;
        // Check generic type alias used without args — error
        if (templateTypeAliases_.count(named->name)) {
            error(typeExpr->loc, "Generic type alias '" + named->name + "' requires type arguments");
            return compiler_.intType();
        }
        // Check template struct/enum names (used without type args — error with helpful message)
        if (templateStructs_.count(named->name)) {
            error(typeExpr->loc, "Template struct '" + named->name + "' requires type arguments");
            return compiler_.intType();
        }
        if (templateEnums_.count(named->name)) {
            error(typeExpr->loc, "Template enum '" + named->name + "' requires type arguments");
            return compiler_.intType();
        }
        error(typeExpr->loc, "Unknown type '" + named->name + "'");
        return compiler_.intType();
    }

    if (typeExpr->kind == ASTNode::ExistentialType) {
        auto* ex = static_cast<ExistentialTypeNode*>(typeExpr);
        if (constraints_.count(ex->constraintName) == 0) {
            error(typeExpr->loc, "Unknown constraint '" + ex->constraintName +
                  "' in existential type `some " + ex->constraintName + "'");
            return compiler_.intType();
        }
        // Only constraints that are object-safe can be existentially quantified.
        ExistentialSafety safety = isExistentialSafe(ex->constraintName);
        if (!safety.ok) {
            error(typeExpr->loc, "Constraint '" + ex->constraintName +
                  "' cannot be used as an existential type `some " + ex->constraintName +
                  "': " + safety.reason);
            return compiler_.intType();
        }
        return existentialTypeFor(ex->constraintName);
    }

    if (typeExpr->kind == ASTNode::ArrayType) {
        auto* arrayNode = static_cast<ArrayTypeNode*>(typeExpr);
        Type* elemType = resolveTypeExpr(arrayNode->elemType.get());
        if (arrayNode->isImmutable) return compiler_.persistentVectorType(elemType);
        return compiler_.arrayType(elemType);
    }

    if (typeExpr->kind == ASTNode::ListType) {
        auto* listNode = static_cast<ListTypeNode*>(typeExpr);
        Type* elemType = resolveTypeExpr(listNode->elemType.get());
        return compiler_.listType(elemType);
    }

    if (typeExpr->kind == ASTNode::RefType) {
        auto* refNode = static_cast<RefTypeNode*>(typeExpr);
        Type* elemType = resolveTypeExpr(refNode->elemType.get());
        return compiler_.refType(elemType);
    }

    if (typeExpr->kind == ASTNode::MapType) {
        auto* mapNode = static_cast<MapTypeNode*>(typeExpr);
        Type* keyType = resolveTypeExpr(mapNode->keyType.get());
        Type* valueType = resolveTypeExpr(mapNode->valueType.get());
        if (mapNode->isImmutable) return compiler_.persistentMapType(keyType, valueType);
        return compiler_.mapType(keyType, valueType);
    }

    if (typeExpr->kind == ASTNode::SetType) {
        auto* setNode = static_cast<SetTypeNode*>(typeExpr);
        Type* elemType = resolveTypeExpr(setNode->elemType.get());
        return compiler_.setType(elemType);
    }

    if (typeExpr->kind == ASTNode::TupleType) {
        auto* tupleNode = static_cast<TupleTypeNode*>(typeExpr);
        if (tupleNode->elemTypes.empty()) {
            return compiler_.voidType();  // () is Void
        }
        Vec<Type*> fields(rt::STLAllocator<Type*>(nullptr));
        for (auto& elem : tupleNode->elemTypes)
            fields.push_back(resolveTypeExpr(elem.get()));
        return compiler_.tupleType(fields);
    }

    if (typeExpr->kind == ASTNode::TemplateType) {
        auto* tmplNode = static_cast<TemplateTypeNode*>(typeExpr);
        // Resolve each type argument
        std::vector<Type*> typeArgs;
        for (auto& arg : tmplNode->typeArgs) {
            typeArgs.push_back(resolveTypeExpr(arg.get()));
        }
        // Look up as template struct
        auto sIt = templateStructs_.find(tmplNode->name);
        if (sIt != templateStructs_.end()) {
            return monomorphizeStruct(tmplNode->name, typeArgs, typeExpr->loc);
        }
        // Look up as template enum
        auto eIt = templateEnums_.find(tmplNode->name);
        if (eIt != templateEnums_.end()) {
            return monomorphizeEnum(tmplNode->name, typeArgs, typeExpr->loc);
        }
        // Look up as generic type alias
        auto taIt = templateTypeAliases_.find(tmplNode->name);
        if (taIt != templateTypeAliases_.end()) {
            return resolveTypeAlias(tmplNode->name, typeArgs, typeExpr->loc);
        }
        // Built-in parameterized types
        if (tmplNode->name == "Coroutine" && typeArgs.size() == 1) {
            return compiler_.coroutineType(typeArgs[0]);
        }
        error(typeExpr->loc, "Unknown template type '" + tmplNode->name + "'");
        return compiler_.intType();
    }

    if (typeExpr->kind == ASTNode::FunctionType) {
        auto* fnNode = static_cast<FunctionTypeNode*>(typeExpr);
        Vec<Type*> paramTypes(rt::STLAllocator<Type*>(nullptr));
        for (auto& param : fnNode->paramTypes) {
            paramTypes.push_back(resolveTypeExpr(param.get()));
        }
        Type* retType = fnNode->returnType ? resolveTypeExpr(fnNode->returnType.get()) : compiler_.voidType();
        return compiler_.functionType(paramTypes, retType);
    }

    error(typeExpr->loc, "Unknown type expression");
    return compiler_.intType();
}

Type* TypeChecker::resolveParamType(FnParam& param) {
    if (param.typeExpr) {
        if (param.isVariadic) {
            return compiler_.arrayType(resolveTypeExpr(param.typeExpr.get()));
        }
        return resolveTypeExpr(param.typeExpr.get());
    }
    // No type annotation — infer from default expression literal
    if (param.defaultExpr) {
        auto* expr = param.defaultExpr.get();
        switch (expr->kind) {
            case ASTNode::IntLiteral:      return compiler_.intType();
            case ASTNode::FloatLiteral:    return compiler_.floatType();
            case ASTNode::StringLiteral:   return compiler_.stringType();
            case ASTNode::BoolLiteral:     return compiler_.boolType();
            case ASTNode::SymbolLiteral:   return compiler_.symbolType();
            case ASTNode::FractionLiteral: return compiler_.fractionType();
            default: break;
        }
        // Try full inference (safe when called during body checking / monomorphization)
        Type* t = inferExpr(static_cast<Expr*>(expr));
        if (t) return t;
        error(param.loc, "Cannot infer type for parameter '" + param.name +
              "' from its default value; add an explicit type annotation");
        return compiler_.intType();
    }
    // No type and no default — should not happen (parser generates synthetic type params)
    return nullptr;
}

std::vector<Type*> TypeChecker::resolveAllParamTypes(std::vector<FnParam>& params) {
    // Check if any default needs inferExpr (non-literal default without type annotation)
    bool needsScope = false;
    for (auto& param : params) {
        if (param.defaultExpr && !param.typeExpr) {
            auto* expr = param.defaultExpr.get();
            switch (expr->kind) {
                case ASTNode::IntLiteral: case ASTNode::FloatLiteral:
                case ASTNode::StringLiteral: case ASTNode::BoolLiteral:
                case ASTNode::SymbolLiteral: case ASTNode::FractionLiteral:
                    break;
                default:
                    needsScope = true;
                    break;
            }
        }
        if (needsScope) break;
    }

    if (needsScope) pushScope();
    std::vector<Type*> paramTypes;
    for (auto& param : params) {
        Type* t = resolveParamType(param);
        paramTypes.push_back(t);
        if (needsScope && t) {
            declareVar(param.name, t, false);
        }
    }
    if (needsScope) popScope();
    return paramTypes;
}

// --- Template struct/enum monomorphization ---

StructType* TypeChecker::monomorphizeStruct(const std::string& name,
                                             const std::vector<Type*>& typeArgs,
                                             SourceRange loc) {
    // Check cache
    MonoKey key{name, typeArgs};
    auto cit = monoStructCache_.find(key);
    if (cit != monoStructCache_.end()) return cit->second;

    auto sIt = templateStructs_.find(name);
    if (sIt == templateStructs_.end()) {
        error(loc, "Unknown template struct '" + name + "'");
        return nullptr;
    }
    StructDeclNode* decl = sIt->second;

    if (typeArgs.size() != decl->typeParams.size()) {
        error(loc, "Template struct '" + name + "' expects " +
              std::to_string(decl->typeParams.size()) + " type arguments, got " +
              std::to_string(typeArgs.size()));
        return nullptr;
    }

    // Save and set type parameter bindings
    auto savedBindings = typeParamBindings_;
    for (size_t i = 0; i < decl->typeParams.size(); ++i) {
        typeParamBindings_[decl->typeParams[i]] = typeArgs[i];
    }

    // Check where constraints on struct
    if (!decl->whereConstraints.empty()) {
        std::unordered_map<std::string, Type*> bindings;
        for (size_t i = 0; i < decl->typeParams.size(); ++i)
            bindings[decl->typeParams[i]] = typeArgs[i];
        if (!checkWhereConstraints(decl->whereConstraints, bindings, name)) {
            typeParamBindings_ = savedBindings;
            return nullptr;
        }
    }

    // Resolve field types with bindings active
    NameTypePairVec fields(rt::STLAllocator<NameTypePair>(nullptr));
    for (auto& field : decl->fields) {
        Type* t = resolveTypeExpr(field.typeExpr.get());
        fields.push_back(NameTypePair{compiler_.intern(field.name), t});
    }

    // Build display name: Name<T1, T2, ...>
    std::string displayName = name + "<";
    for (size_t i = 0; i < typeArgs.size(); ++i) {
        if (i > 0) displayName += ", ";
        auto s = typeArgs[i]->str();
        displayName += std::string(s.data(), s.size());
    }
    displayName += ">";

    // Create the monomorphized StructType
    auto* structType = new StructType(compiler_.intern(displayName), std::move(fields), decl->isTupleStruct);

    // Cache and register (by display name for pattern matching lookup)
    monoStructCache_[key] = structType;
    structTypes_[displayName] = structType;
    monoOrigin_[structType] = MonoOrigin{name, typeArgs};

    // Restore bindings
    typeParamBindings_ = savedBindings;

    return structType;
}

EnumType* TypeChecker::monomorphizeEnum(const std::string& name,
                                         const std::vector<Type*>& typeArgs,
                                         SourceRange loc) {
    // Check cache
    MonoKey key{name, typeArgs};
    auto cit = monoEnumCache_.find(key);
    if (cit != monoEnumCache_.end()) return cit->second;

    // Built-in Option<T>: delegate to VM's canonical optionType() for pointer identity
    if (name == "Option" && typeArgs.size() == 1) {
        auto* enumType = compiler_.optionType(typeArgs[0]);
        monoEnumCache_[key] = enumType;
        monoOrigin_[enumType] = MonoOrigin{name, typeArgs};
        auto nameStr = enumType->str();
        enumTypes_[std::string(nameStr.data(), nameStr.size())] = enumType;
        return enumType;
    }

    auto eIt = templateEnums_.find(name);
    if (eIt == templateEnums_.end()) {
        error(loc, "Unknown template enum '" + name + "'");
        return nullptr;
    }
    UnionDeclNode* decl = eIt->second;

    if (typeArgs.size() != decl->typeParams.size()) {
        error(loc, "Template enum '" + name + "' expects " +
              std::to_string(decl->typeParams.size()) + " type arguments, got " +
              std::to_string(typeArgs.size()));
        return nullptr;
    }

    // Save and set type parameter bindings
    auto savedBindings = typeParamBindings_;
    for (size_t i = 0; i < decl->typeParams.size(); ++i) {
        typeParamBindings_[decl->typeParams[i]] = typeArgs[i];
    }

    // Check where constraints on enum
    if (!decl->whereConstraints.empty()) {
        std::unordered_map<std::string, Type*> bindings;
        for (size_t i = 0; i < decl->typeParams.size(); ++i)
            bindings[decl->typeParams[i]] = typeArgs[i];
        if (!checkWhereConstraints(decl->whereConstraints, bindings, name)) {
            typeParamBindings_ = savedBindings;
            return nullptr;
        }
    }

    // Resolve case types with bindings active
    NameTypePairVec cases(rt::STLAllocator<NameTypePair>(nullptr));
    for (auto& ucase : decl->cases) {
        Type* t = ucase.typeExpr ? resolveTypeExpr(ucase.typeExpr.get()) : compiler_.voidType();
        cases.push_back(NameTypePair{compiler_.intern(ucase.name), t});
    }

    // Build display name: Name<T1, T2, ...>
    std::string displayName = name + "<";
    for (size_t i = 0; i < typeArgs.size(); ++i) {
        if (i > 0) displayName += ", ";
        auto s = typeArgs[i]->str();
        displayName += std::string(s.data(), s.size());
    }
    displayName += ">";

    // Create the monomorphized EnumType
    auto* enumType = new EnumType(compiler_.intern(displayName), std::move(cases));

    // Cache and register (by display name for pattern matching lookup)
    monoEnumCache_[key] = enumType;
    enumTypes_[displayName] = enumType;
    monoOrigin_[enumType] = MonoOrigin{name, typeArgs};

    // Restore bindings
    typeParamBindings_ = savedBindings;

    return enumType;
}

// --- Generic type alias resolution ---

Type* TypeChecker::resolveTypeAlias(const std::string& name,
                                     const std::vector<Type*>& typeArgs,
                                     SourceRange loc) {
    // Check cache
    MonoKey key{name, typeArgs};
    auto cit = monoAliasCache_.find(key);
    if (cit != monoAliasCache_.end()) return cit->second;

    auto taIt = templateTypeAliases_.find(name);
    if (taIt == templateTypeAliases_.end()) {
        error(loc, "Unknown generic type alias '" + name + "'");
        return compiler_.intType();
    }
    TypeAliasDeclNode* decl = taIt->second;

    if (typeArgs.size() != decl->typeParams.size()) {
        error(loc, "Generic type alias '" + name + "' expects " +
              std::to_string(decl->typeParams.size()) + " type arguments, got " +
              std::to_string(typeArgs.size()));
        return compiler_.intType();
    }

    // Save and set type parameter bindings
    auto savedBindings = typeParamBindings_;
    for (size_t i = 0; i < decl->typeParams.size(); ++i) {
        typeParamBindings_[decl->typeParams[i]] = typeArgs[i];
    }

    // Check where constraints on type alias
    if (!decl->whereConstraints.empty()) {
        std::unordered_map<std::string, Type*> bindings;
        for (size_t i = 0; i < decl->typeParams.size(); ++i)
            bindings[decl->typeParams[i]] = typeArgs[i];
        if (!checkWhereConstraints(decl->whereConstraints, bindings, name)) {
            typeParamBindings_ = savedBindings;
            return compiler_.intType();
        }
    }

    // Resolve the alias body with bindings active
    Type* resolved = resolveTypeExpr(decl->typeExpr.get());

    // Cache the result
    monoAliasCache_[key] = resolved;

    // Restore bindings
    typeParamBindings_ = savedBindings;

    return resolved;
}

} // namespace ts
