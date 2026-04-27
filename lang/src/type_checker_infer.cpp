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
//  type_checker_infer.cpp
//  lang
//
//  Type checker -- expression type inference dispatcher
//

#include "type_checker.hpp"
#include "module_compiler.hpp"
#include "builtins.hpp"
#include "value.hpp"
#include "diagnostic.hpp"

namespace ts {

// --- Infer expression types ---

Type* TypeChecker::inferExpr(Expr* expr, Type* expectedType) {
    if (!expr) return nullptr;

    Type* result = nullptr;

    switch (expr->kind) {
        case ASTNode::IntLiteral:
            result = compiler_.intType();
            break;

        case ASTNode::FloatLiteral:
            result = compiler_.floatType();
            break;

        case ASTNode::ImaginaryLiteral:
            result = compiler_.complexType();
            break;

        case ASTNode::FractionLiteral:
            result = compiler_.fractionType();
            break;

        case ASTNode::StringLiteral:
            result = compiler_.stringType();
            break;

        case ASTNode::BoolLiteral:
            result = compiler_.boolType();
            break;

        case ASTNode::SymbolLiteral:
            result = compiler_.symbolType();
            break;

        case ASTNode::Identifier:
            result = inferIdentifier(static_cast<IdentifierExpr*>(expr));
            break;

        case ASTNode::DynamicVar: {
            auto* dv = static_cast<DynamicVarExpr*>(expr);
            auto* dvi = compiler_.lookupDynVar(dv->name);
            if (!dvi) {
                error(expr->loc, "Undeclared dynamic variable '`" + dv->name + "'");
                result = compiler_.intType();
            } else {
                result = dvi->type;
            }
            break;
        }

        case ASTNode::BinaryOp:
            result = inferBinaryOp(static_cast<BinaryOpExpr*>(expr));
            break;

        case ASTNode::UnaryOp:
            result = inferUnaryOp(static_cast<UnaryOpExpr*>(expr));
            break;

        case ASTNode::CallExpr:
            result = inferCall(static_cast<CallExpr_*>(expr));
            break;

        case ASTNode::NilLiteral:
            result = nullptr;  // polymorphic nil, resolved contextually
            break;

        case ASTNode::ListLiteral: {
            auto* lit = static_cast<ListLiteralExpr*>(expr);
            if (lit->elements.empty()) {
                if (auto* lt = dynamic_cast<ListType*>(expectedType)) {
                    result = lt;
                } else {
                    error(expr->loc, "Cannot infer type of empty List() literal; use 'let x List<T> = nil' instead");
                    result = compiler_.intType();
                }
            } else {
                Type* elemType = inferExpr(static_cast<Expr*>(lit->elements[0].get()));
                for (size_t i = 1; i < lit->elements.size(); ++i) {
                    Type* t = inferExpr(static_cast<Expr*>(lit->elements[i].get()));
                    if (isNumeric(elemType) && isNumeric(t)) {
                        elemType = commonNumericType(elemType, t);
                    } else if (elemType != t) {
                        error(lit->elements[i]->loc, "List element type mismatch");
                    }
                }
                result = compiler_.listType(elemType);
            }
            break;
        }

        case ASTNode::ArrayLiteral: {
            auto* lit = static_cast<ArrayLiteralExpr*>(expr);
            if (lit->elemTypeExpr) {
                // Typed array constructor: [Type](...)
                Type* elemType = resolveTypeExpr(lit->elemTypeExpr.get());
                for (auto& elem : lit->elements) {
                    Type* et = inferExpr(static_cast<Expr*>(elem.get()));
                    if (et && !isAssignable(et, elemType)) {
                        if (isNumeric(et) && isNumeric(elemType)) {
                            // Allow numeric promotion (e.g. Int -> Float)
                        } else {
                            error(elem->loc, "Array element type mismatch: expected " +
                                  std::string(elemType->str()) + " but got " + std::string(et->str()));
                        }
                    }
                }
                result = compiler_.arrayType(elemType);
            } else if (lit->elements.empty()) {
                if (auto* at = dynamic_cast<ArrayType*>(expectedType)) {
                    result = at;
                } else {
                    error(expr->loc, "Cannot infer type of empty array literal");
                    result = compiler_.intType();
                }
            } else {
                // Detect explicit @ annotations on elements
                std::vector<AutoMapArg> autoMap(lit->elements.size());
                bool anyAutoMap = false;

                for (size_t i = 0; i < lit->elements.size(); ++i) {
                    Expr* elem = static_cast<Expr*>(lit->elements[i].get());
                    if (elem->kind == ASTNode::AutoMap) {
                        auto* am = static_cast<AutoMapExpr*>(elem);
                        autoMap[i].depth = am->depth;
                        autoMap[i].cartesianIndex = am->cartesianIndex;
                        anyAutoMap = true;
                    }
                }

                // Infer all element types
                std::vector<Type*> elemTypes;
                for (size_t i = 0; i < lit->elements.size(); ++i) {
                    elemTypes.push_back(inferExpr(static_cast<Expr*>(lit->elements[i].get())));
                }

                if (anyAutoMap) {
                    // Unwrap @-tagged element types to get inner types
                    std::vector<Type*> unwrappedTypes;
                    for (size_t i = 0; i < elemTypes.size(); ++i) {
                        if (autoMap[i].depth > 0) {
                            bool isList = false;
                            Type* inner = unwrapAutoMapLayers(elemTypes[i], autoMap[i].depth, isList, lit->elements[i]->loc);
                            autoMap[i].isList = isList;
                            unwrappedTypes.push_back(inner);
                        } else {
                            unwrappedTypes.push_back(elemTypes[i]);
                        }
                    }

                    // Compute common element type from unwrapped types
                    Type* elemType = unwrappedTypes[0];
                    for (size_t i = 1; i < unwrappedTypes.size(); ++i) {
                        if (isNumeric(elemType) && isNumeric(unwrappedTypes[i])) {
                            elemType = commonNumericType(elemType, unwrappedTypes[i]);
                        } else if (elemType != unwrappedTypes[i]) {
                            error(lit->elements[i]->loc, "Array element type mismatch");
                        }
                    }

                    lit->autoMapElements = std::move(autoMap);

                    // Compute result type based on cartesian vs zip
                    int maxCartesian = 0;
                    for (auto& am : lit->autoMapElements) {
                        if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
                    }

                    // Inner type is [elemType] (each produced inner element is an array)
                    Type* innerArrayType = compiler_.arrayType(elemType);
                    // Wrap in Array levels: zip = 1 outer level, cartesian = maxCartesian outer levels
                    int wrapLevels = (maxCartesian > 0) ? maxCartesian : 1;
                    Type* wrapped = innerArrayType;
                    for (int level = 0; level < wrapLevels; ++level) {
                        wrapped = compiler_.arrayType(wrapped);
                    }
                    result = wrapped;
                } else {
                    Type* elemType = elemTypes[0];
                    for (size_t i = 1; i < elemTypes.size(); ++i) {
                        if (isNumeric(elemType) && isNumeric(elemTypes[i]) &&
                            numericRank(elemType) <= 4 && numericRank(elemTypes[i]) <= 4) {
                            // Only promote between scalar numeric types in array literals
                            elemType = commonNumericType(elemType, elemTypes[i]);
                        } else if (elemType != elemTypes[i]) {
                            error(lit->elements[i]->loc, "Array element type mismatch: expected '" +
                                  std::string(elemType->str().data(), elemType->str().size()) +
                                  "', got '" + std::string(elemTypes[i]->str().data(), elemTypes[i]->str().size()) + "'");
                        }
                    }
                    result = compiler_.arrayType(elemType);
                }
            }
            break;
        }

        case ASTNode::MapLiteral: {
            auto* lit = static_cast<MapLiteralExpr*>(expr);
            if (lit->entries.empty()) {
                if (auto* mt = dynamic_cast<MapType*>(expectedType)) {
                    result = mt;
                } else {
                    error(expr->loc, "Cannot infer type of empty map literal [:]");
                    result = compiler_.intType();
                }
            } else {
                Type* keyType = inferExpr(static_cast<Expr*>(lit->entries[0].key.get()));
                Type* valType = inferExpr(static_cast<Expr*>(lit->entries[0].value.get()));
                for (size_t i = 1; i < lit->entries.size(); ++i) {
                    Type* kt = inferExpr(static_cast<Expr*>(lit->entries[i].key.get()));
                    Type* vt = inferExpr(static_cast<Expr*>(lit->entries[i].value.get()));
                    if (isNumeric(keyType) && isNumeric(kt)) {
                        keyType = commonNumericType(keyType, kt);
                    } else if (keyType != kt) {
                        error(lit->entries[i].key->loc, "Map key type mismatch");
                    }
                    if (isNumeric(valType) && isNumeric(vt)) {
                        valType = commonNumericType(valType, vt);
                    } else if (valType != vt) {
                        error(lit->entries[i].value->loc, "Map value type mismatch");
                    }
                }
                result = compiler_.mapType(keyType, valType);
            }
            break;
        }

        case ASTNode::SetLiteral: {
            auto* lit = static_cast<SetLiteralExpr*>(expr);
            if (lit->elements.empty()) {
                if (auto* st = dynamic_cast<SetType*>(expectedType)) {
                    result = st;
                } else {
                    error(expr->loc, "Cannot infer type of empty Set() literal");
                    result = compiler_.intType();
                }
            } else {
                Type* elemType = inferExpr(static_cast<Expr*>(lit->elements[0].get()));
                for (size_t i = 1; i < lit->elements.size(); ++i) {
                    Type* t = inferExpr(static_cast<Expr*>(lit->elements[i].get()));
                    if (isNumeric(elemType) && isNumeric(t)) {
                        elemType = commonNumericType(elemType, t);
                    } else if (elemType != t) {
                        error(lit->elements[i]->loc, "Set element type mismatch");
                    }
                }
                result = compiler_.setType(elemType);
            }
            break;
        }

        case ASTNode::TupleLiteral: {
            auto* lit = static_cast<TupleLiteralExpr*>(expr);

            // Detect explicit @ annotations on elements
            std::vector<AutoMapArg> autoMap(lit->elements.size());
            bool anyAutoMap = false;

            for (size_t i = 0; i < lit->elements.size(); ++i) {
                Expr* elem = static_cast<Expr*>(lit->elements[i].get());
                if (elem->kind == ASTNode::AutoMap) {
                    auto* am = static_cast<AutoMapExpr*>(elem);
                    autoMap[i].depth = am->depth;
                    autoMap[i].cartesianIndex = am->cartesianIndex;
                    anyAutoMap = true;
                }
            }

            // Infer all element types
            Vec<Type*> fieldTypes(rt::STLAllocator<Type*>(nullptr));
            for (auto& elem : lit->elements) {
                fieldTypes.push_back(inferExpr(static_cast<Expr*>(elem.get())));
            }

            if (anyAutoMap) {
                // Unwrap @-tagged element types to get inner tuple field types
                Vec<Type*> unwrappedTypes(rt::STLAllocator<Type*>(nullptr));
                for (size_t i = 0; i < fieldTypes.size(); ++i) {
                    if (autoMap[i].depth > 0) {
                        bool isList = false;
                        Type* inner = unwrapAutoMapLayers(fieldTypes[i], autoMap[i].depth, isList, lit->elements[i]->loc);
                        autoMap[i].isList = isList;
                        unwrappedTypes.push_back(inner);
                    } else {
                        unwrappedTypes.push_back(fieldTypes[i]);
                    }
                }

                lit->autoMapElements = std::move(autoMap);

                // The inner tuple type uses unwrapped types
                Type* tupleType = compiler_.tupleType(unwrappedTypes);

                // Compute result type based on cartesian vs zip
                int maxCartesian = 0;
                for (auto& am : lit->autoMapElements) {
                    if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
                }

                // Wrap in Array levels: zip = 1 outer level, cartesian = maxCartesian outer levels
                // tupleType is already the inner element type
                // For zip @: result = [[tupleType]]... no, result = [tupleType]
                // Actually for tuples: ([1,2,3]@, 'b, 'c) -> [(1,'b,'c),...] = [(Int, Symbol, Symbol)]
                // So only wrap maxCartesian (or 1) times total, starting from tupleType
                int wrapLevels = (maxCartesian > 0) ? maxCartesian : 1;
                Type* wrapped = static_cast<Type*>(tupleType);
                for (int level = 0; level < wrapLevels; ++level) {
                    wrapped = compiler_.arrayType(wrapped);
                }
                result = wrapped;
            } else {
                result = compiler_.tupleType(fieldTypes);
            }
            break;
        }

        case ASTNode::StructLiteral: {
            auto* lit = static_cast<StructLiteralExpr*>(expr);
            StructType* stype = nullptr;

            // Try non-template struct first
            auto it = structTypes_.find(lit->structName);
            if (it != structTypes_.end()) {
                stype = it->second;
            }

            // If not found, try type alias that resolves to a struct
            if (!stype) {
                auto ait = typeAliases_.find(lit->structName);
                if (ait != typeAliases_.end()) {
                    stype = dynamic_cast<StructType*>(ait->second);
                }
            }

            // If not found, try template struct
            if (!stype) {
                auto tmplIt = templateStructs_.find(lit->structName);
                if (tmplIt != templateStructs_.end()) {
                    StructDeclNode* decl = tmplIt->second;

                    if (!lit->typeArgs.empty()) {
                        // Explicit type args: Pair<Int, Float> { ... }
                        std::vector<Type*> typeArgs;
                        for (auto& ta : lit->typeArgs) {
                            typeArgs.push_back(resolveTypeExpr(ta.get()));
                        }
                        stype = monomorphizeStruct(lit->structName, typeArgs, expr->loc);
                    } else {
                        // If spread is present, try to infer from spread type first
                        if (lit->spreadExpr) {
                            Type* spreadType = inferExpr(static_cast<Expr*>(lit->spreadExpr.get()));
                            auto* spreadStruct = dynamic_cast<StructType*>(spreadType);
                            if (spreadStruct && spreadStruct->name_->str().find(lit->structName) != std::string::npos) {
                                stype = spreadStruct;
                                // Assign field names for positional construction
                                if (lit->positional) {
                                    for (size_t i = 0; i < lit->fields.size() && i < stype->fields_.size(); ++i) {
                                        lit->fields[i].name = std::string(stype->fields_[i].name->str());
                                    }
                                }
                                // Validate field count (spread fills remaining)
                                if (lit->fields.size() > stype->fields_.size()) {
                                    error(expr->loc, "Struct '" + lit->structName + "' expects " +
                                          std::to_string(stype->fields_.size()) + " fields, got " +
                                          std::to_string(lit->fields.size()));
                                }
                                result = stype;
                                break;
                            }
                        }

                        // Infer type args from field values
                        // First, type-check all field values
                        std::vector<Type*> fieldTypes;
                        for (size_t i = 0; i < lit->fields.size(); ++i) {
                            fieldTypes.push_back(inferExpr(static_cast<Expr*>(lit->fields[i].value.get())));
                        }

                        // Build field name -> type mapping
                        std::unordered_map<std::string, Type*> fieldTypeMap;
                        if (lit->positional) {
                            for (size_t i = 0; i < fieldTypes.size() && i < decl->fields.size(); ++i) {
                                fieldTypeMap[decl->fields[i].name] = fieldTypes[i];
                            }
                        } else {
                            for (size_t i = 0; i < lit->fields.size(); ++i) {
                                fieldTypeMap[lit->fields[i].name] = fieldTypes[i];
                            }
                        }

                        // Unify each declared field's type expr with the actual type
                        std::unordered_map<std::string, Type*> bindings;
                        bool ok = true;
                        for (auto& field : decl->fields) {
                            auto ftIt = fieldTypeMap.find(field.name);
                            if (ftIt != fieldTypeMap.end() && ftIt->second) {
                                if (!unifyTypeExpr(field.typeExpr.get(), ftIt->second,
                                                   decl->typeParams, bindings)) {
                                    ok = false;
                                    break;
                                }
                            }
                        }

                        if (ok) {
                            // Check all type params are bound
                            std::vector<Type*> typeArgs;
                            for (auto& tp : decl->typeParams) {
                                auto bIt = bindings.find(tp);
                                if (bIt == bindings.end()) {
                                    error(expr->loc, "Cannot infer type parameter '" + tp +
                                          "' for template struct '" + lit->structName + "'");
                                    ok = false;
                                    break;
                                }
                                typeArgs.push_back(bIt->second);
                            }
                            if (ok) {
                                stype = monomorphizeStruct(lit->structName, typeArgs, expr->loc);
                            }
                        }

                        if (!ok || !stype) {
                            error(expr->loc, "Cannot infer type arguments for template struct '" +
                                  lit->structName + "'");
                            result = compiler_.intType();
                            break;
                        }

                        // Assign field names for positional construction
                        if (lit->positional) {
                            for (size_t i = 0; i < lit->fields.size() && i < stype->fields_.size(); ++i) {
                                lit->fields[i].name = std::string(stype->fields_[i].name->str());
                            }
                        }

                        // Fields already type-checked, validate against monomorphized type
                        if (!lit->spreadExpr && lit->fields.size() != stype->fields_.size()) {
                            error(expr->loc, "Struct '" + lit->structName + "' expects " +
                                  std::to_string(stype->fields_.size()) + " fields, got " +
                                  std::to_string(lit->fields.size()));
                        } else if (lit->spreadExpr && lit->fields.size() > stype->fields_.size()) {
                            error(expr->loc, "Struct '" + lit->structName + "' expects " +
                                  std::to_string(stype->fields_.size()) + " fields, got " +
                                  std::to_string(lit->fields.size()));
                        }
                        result = stype;
                        break;
                    }
                } else {
                    error(expr->loc, "Unknown struct type '" + lit->structName + "'");
                    result = compiler_.intType();
                    break;
                }
            }

            if (!stype) {
                result = compiler_.intType();
                break;
            }

            // Validate fields match declaration
            if (lit->spreadExpr) {
                // With spread, fewer fields are allowed
                if (lit->fields.size() > stype->fields_.size()) {
                    error(expr->loc, "Struct '" + lit->structName + "' expects " +
                          std::to_string(stype->fields_.size()) + " fields, got " +
                          std::to_string(lit->fields.size()));
                }
                // Type-check the spread expression
                Type* spreadType = inferExpr(static_cast<Expr*>(lit->spreadExpr.get()));
                if (!typesEqual(spreadType, stype)) {
                    error(lit->spreadExpr->loc, "Spread expression type does not match struct '" +
                          lit->structName + "'");
                }
            } else if (lit->fields.size() != stype->fields_.size()) {
                error(expr->loc, "Struct '" + lit->structName + "' expects " +
                      std::to_string(stype->fields_.size()) + " fields, got " +
                      std::to_string(lit->fields.size()));
            }

            // For positional construction, assign field names from the struct declaration
            if (lit->positional) {
                for (size_t i = 0; i < lit->fields.size() && i < stype->fields_.size(); ++i) {
                    lit->fields[i].name = std::string(stype->fields_[i].name->str());
                }
            }

            // Type-check each field, verify names match, and detect auto-mapping
            bool anyAutoMap = false;
            std::vector<AutoMapArg> autoMap(lit->fields.size());

            for (size_t i = 0; i < lit->fields.size(); ++i) {
                // Check for explicit @-annotation on field value
                Expr* fieldExpr = static_cast<Expr*>(lit->fields[i].value.get());
                AutoMapArg explicitAM;
                if (fieldExpr->kind == ASTNode::AutoMap) {
                    auto* am = static_cast<AutoMapExpr*>(fieldExpr);
                    explicitAM.depth = am->depth;
                    explicitAM.cartesianIndex = am->cartesianIndex;
                }

                Type* fieldType = inferExpr(fieldExpr);

                // Find the field in the struct type by name
                bool found = false;
                for (size_t j = 0; j < stype->fields_.size(); ++j) {
                    if (stype->fields_[j].name->str() == lit->fields[i].name) {
                        found = true;
                        Type* declType = stype->fields_[j].type;

                        if (explicitAM.depth > 0) {
                            // Explicit @ on this field
                            auto* arrT = dynamic_cast<ArrayType*>(fieldType);
                            if (!arrT) {
                                error(lit->fields[i].loc, "Explicit '@' on field '" +
                                      lit->fields[i].name + "' requires Array type");
                            } else {
                                // Validate inner type matches declared type
                                Type* innerType = arrT->elemType_;
                                if (!typesEqual(innerType, declType) &&
                                    !(declType == compiler_.floatType() && innerType == compiler_.intType())) {
                                    error(lit->fields[i].loc, "Field '" + lit->fields[i].name +
                                          "' type mismatch in struct '" + lit->structName + "'");
                                }
                            }
                            autoMap[i] = explicitAM;
                            anyAutoMap = true;
                        } else if (fieldType && !typesEqual(fieldType, declType)) {
                            if (declType == compiler_.floatType() && fieldType == compiler_.intType()) {
                                // promotion OK
                            } else if (auto* arrT = dynamic_cast<ArrayType*>(fieldType)) {
                                // Implicit auto-mapping: [T] provided where T expected
                                if (typesEqual(arrT->elemType_, declType) ||
                                    (declType == compiler_.floatType() && arrT->elemType_ == compiler_.intType())) {
                                    autoMap[i] = AutoMapArg{1, 0};
                                    anyAutoMap = true;
                                } else {
                                    error(lit->fields[i].loc, "Field '" + lit->fields[i].name +
                                          "' type mismatch in struct '" + lit->structName + "'");
                                }
                            } else {
                                error(lit->fields[i].loc, "Field '" + lit->fields[i].name +
                                      "' type mismatch in struct '" + lit->structName + "'");
                            }
                        }
                        break;
                    }
                }
                if (!found) {
                    error(lit->fields[i].loc, "Unknown field '" + lit->fields[i].name +
                          "' in struct '" + lit->structName + "'");
                }
            }

            if (anyAutoMap) {
                lit->autoMapFields = std::move(autoMap);
                // Compute cartesian nesting depth
                int maxCartesian = 0;
                for (auto& am : lit->autoMapFields) {
                    if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
                }
                int wrapLevels = (maxCartesian > 0) ? maxCartesian : 1;
                Type* wrapped = static_cast<Type*>(stype);
                for (int level = 0; level < wrapLevels; ++level) {
                    wrapped = compiler_.arrayType(wrapped);
                }
                result = wrapped;
            } else {
                result = stype;
            }
            break;
        }

        case ASTNode::EnumConstructor: {
            // This node may be a re-tagged CallExpr_ or FieldExpr_ from a previous
            // type-check pass (e.g., recheckTemplateBody).  In that case, the
            // resolvedType is already set correctly — reuse it to avoid UB from
            // static_cast<EnumConstructExpr*> on a node that isn't one.
            if (auto* existingEnum = dynamic_cast<EnumType*>(expr->resolvedType)) {
                result = existingEnum;
                break;
            }

            auto* ec = static_cast<EnumConstructExpr*>(expr);
            EnumType* etype = nullptr;

            // Try non-template enum first
            auto it = enumTypes_.find(ec->enumName);
            if (it != enumTypes_.end()) {
                etype = it->second;
            }

            // If not found, try template enum
            if (!etype) {
                auto tmplIt = templateEnums_.find(ec->enumName);
                if (tmplIt != templateEnums_.end()) {
                    UnionDeclNode* decl = tmplIt->second;

                    if (!ec->typeArgs.empty()) {
                        // Explicit type args: Option<Int>.some(42) or Option<Int>.none
                        std::vector<Type*> typeArgs;
                        for (auto& ta : ec->typeArgs) {
                            typeArgs.push_back(resolveTypeExpr(ta.get()));
                        }
                        etype = monomorphizeEnum(ec->enumName, typeArgs, expr->loc);
                    } else if (ec->arg) {
                        // Infer type args from the argument
                        Type* argType = inferExpr(static_cast<Expr*>(ec->arg.get()));
                        // Find the case in the template declaration
                        for (auto& ucase : decl->cases) {
                            if (ucase.name == ec->caseName && ucase.typeExpr) {
                                std::unordered_map<std::string, Type*> bindings;
                                if (argType && unifyTypeExpr(ucase.typeExpr.get(), argType,
                                                              decl->typeParams, bindings)) {
                                    std::vector<Type*> typeArgs;
                                    bool allBound = true;
                                    for (auto& tp : decl->typeParams) {
                                        auto bIt = bindings.find(tp);
                                        if (bIt == bindings.end()) {
                                            allBound = false;
                                            break;
                                        }
                                        typeArgs.push_back(bIt->second);
                                    }
                                    if (allBound) {
                                        etype = monomorphizeEnum(ec->enumName, typeArgs, expr->loc);
                                    }
                                }
                                break;
                            }
                        }
                        if (!etype) {
                            error(expr->loc, "Cannot infer type arguments for template enum '" +
                                  ec->enumName + "'");
                            result = compiler_.intType();
                            break;
                        }
                        // Validate the already-inferred arg against the monomorphized case type
                        for (size_t i = 0; i < etype->cases_.size(); ++i) {
                            if (etype->cases_[i].name->str() == ec->caseName) {
                                Type* caseType = etype->cases_[i].type;
                                if (argType && !typesEqual(argType, caseType)) {
                                    if (isAssignable(argType, caseType)) {
                                        ec->arg->resolvedType = caseType;
                                    } else {
                                        error(ec->arg->loc, "Enum case '" + ec->caseName +
                                              "' expects type '" + std::string(caseType->str().data(), caseType->str().size()) + "'");
                                    }
                                }
                                break;
                            }
                        }
                        result = etype;
                        break;
                    } else {
                        // No-data case without explicit type args: cannot infer
                        error(expr->loc, "Cannot infer type parameters for no-data enum case '" +
                              ec->caseName + "'; use explicit type arguments: " +
                              ec->enumName + "<...>." + ec->caseName);
                        result = compiler_.intType();
                        break;
                    }
                } else {
                    error(expr->loc, "Unknown enum type '" + ec->enumName + "'");
                    result = compiler_.intType();
                    break;
                }
            }

            if (!etype) {
                result = compiler_.intType();
                break;
            }

            // Find the case (for non-template or explicit-type-arg template)
            bool found = false;
            for (size_t i = 0; i < etype->cases_.size(); ++i) {
                if (etype->cases_[i].name->str() == ec->caseName) {
                    found = true;
                    Type* caseType = etype->cases_[i].type;
                    if (ec->arg) {
                        Type* argType = inferExpr(static_cast<Expr*>(ec->arg.get()));
                        if (caseType == compiler_.voidType()) {
                            error(ec->arg->loc, "Enum case '" + ec->caseName + "' takes no data");
                        } else if (argType && !typesEqual(argType, caseType)) {
                            if (isAssignable(argType, caseType)) {
                                ec->arg->resolvedType = caseType;
                            } else {
                                error(ec->arg->loc, "Enum case '" + ec->caseName +
                                      "' expects type '" + std::string(caseType->str().data(), caseType->str().size()) + "'");
                            }
                        }
                    } else {
                        if (caseType != compiler_.voidType()) {
                            error(expr->loc, "Enum case '" + ec->caseName + "' requires a value");
                        }
                    }
                    break;
                }
            }
            if (!found) {
                error(expr->loc, "Unknown case '" + ec->caseName + "' in enum '" + ec->enumName + "'");
            }

            result = etype;
            break;
        }

        case ASTNode::AutoMap: {
            // Explicit postfix auto-map: expr @, expr @@, expr @1, expr @2
            // The AutoMapExpr itself resolves to the inner expression's type;
            // the actual unwrapping/wrapping happens at the call site in inferCall.
            auto* am = static_cast<AutoMapExpr*>(expr);
            result = inferExpr(static_cast<Expr*>(am->inner.get()));
            break;
        }

        case ASTNode::RangeExpr: {
            auto* range = static_cast<RangeExprNode*>(expr);
            Type* startType = inferExpr(static_cast<Expr*>(range->start.get()));
            Type* elemType = startType;

            if (startType != compiler_.intType() && startType != compiler_.fractionType()) {
                error(range->start->loc, "Range start must be Int or Fraction");
                elemType = compiler_.intType();
            }

            if (range->next) {
                Type* nextType = inferExpr(static_cast<Expr*>(range->next.get()));
                if (nextType != compiler_.intType() && nextType != compiler_.fractionType()) {
                    error(range->next->loc, "Range step value must be Int or Fraction");
                }
                if (isNumeric(elemType) && isNumeric(nextType)) {
                    elemType = commonNumericType(elemType, nextType);
                }
            }

            if (range->end) {
                Type* endType = inferExpr(static_cast<Expr*>(range->end.get()));
                if (endType != compiler_.intType() && endType != compiler_.fractionType()) {
                    error(range->end->loc, "Range end must be Int or Fraction");
                }
                if (isNumeric(elemType) && isNumeric(endType)) {
                    elemType = commonNumericType(elemType, endType);
                }
            }

            result = compiler_.rangeType(elemType);
            break;
        }

        case ASTNode::AsTypeExpr: {
            auto* node = static_cast<AsTypeExprNode*>(expr);
            Type* subjType = inferExpr(static_cast<Expr*>(node->subject.get()));
            Type* targetType = resolveTypeExpr(node->targetType.get());
            node->resolvedTargetType = targetType;
            if (subjType && dynamic_cast<AnyType*>(subjType)) {
                // Any -> concrete type: fallible, returns Option
                result = compiler_.optionType(targetType);
            } else if (subjType && isNumeric(subjType) && isNumeric(targetType)
                       && numericRank(subjType) > numericRank(targetType)
                       && numericRank(subjType) <= 4 && numericRank(targetType) >= 1) {
                // Numeric downcast (not to Bool): infallible, returns target type
                result = targetType;
            } else if (subjType) {
                // Try auto-mapping: unwrap Array/List layers to find a numeric element type
                Type* elemType = subjType;
                int depth = 0;
                bool isList = false;
                while (depth < 8) {
                    if (auto* arrT = dynamic_cast<ArrayType*>(elemType)) {
                        elemType = arrT->elemType_;
                        ++depth;
                    } else if (auto* lstT = dynamic_cast<ListType*>(elemType)) {
                        elemType = lstT->elemType_;
                        isList = true;
                        ++depth;
                    } else {
                        break;
                    }
                }
                if (depth > 0 && isNumeric(elemType) && isNumeric(targetType)
                    && numericRank(elemType) > numericRank(targetType)
                    && numericRank(elemType) <= 4 && numericRank(targetType) >= 1) {
                    node->autoMapDepth = depth;
                    node->autoMapIsList = isList;
                    // Wrap target type in the same number of Array/List layers
                    result = targetType;
                    for (int i = 0; i < depth; ++i) {
                        if (isList) {
                            result = compiler_.listType(result);
                        } else {
                            result = compiler_.arrayType(result);
                        }
                    }
                } else {
                    error(node->loc, "as(Type) requires an Any value or a numeric downcast");
                }
            }
            break;
        }

        case ASTNode::LambdaExpr:
            result = inferLambdaExpr(static_cast<LambdaExprNode*>(expr));
            break;

        case ASTNode::IfExpr: {
            auto* ie = static_cast<IfExprNode*>(expr);
            Type* condType = inferExpr(static_cast<Expr*>(ie->condition.get()));
            if (condType && condType != compiler_.boolType() && condType != compiler_.intType()) {
                error(ie->condition->loc, "Ternary condition must be bool or int");
            }
            // Type-check both branches
            checkNode(ie->thenBranch.get());
            if (ie->elseBranch) {
                checkNode(ie->elseBranch.get());
            }
            // Infer type from trailing expression in then branch
            if (ie->thenBranch->kind == ASTNode::Block) {
                auto* block = static_cast<BlockStmt*>(ie->thenBranch.get());
                if (!block->stmts.empty()) {
                    auto* last = block->stmts.back().get();
                    if (last->kind == ASTNode::ExprStmt) {
                        auto* es = static_cast<ExprStmtNode*>(last);
                        if (es->isTrailing) {
                            result = es->expr->resolvedType;
                        }
                    }
                }
            }
            if (!result) result = compiler_.voidType();
            break;
        }

        case ASTNode::BlockExpr: {
            auto* be = static_cast<BlockExprNode*>(expr);
            checkNode(be->body.get());
            if (be->body->kind == ASTNode::Block) {
                auto* block = static_cast<BlockStmt*>(be->body.get());
                if (!block->stmts.empty()) {
                    auto* last = block->stmts.back().get();
                    if (last->kind == ASTNode::ExprStmt) {
                        auto* es = static_cast<ExprStmtNode*>(last);
                        if (es->isTrailing) {
                            result = es->expr->resolvedType;
                        }
                    }
                }
            }
            if (!result) result = compiler_.voidType();
            break;
        }

        case ASTNode::FieldExpr: {
            auto* fe = static_cast<FieldExpr_*>(expr);

            // Check for std-qualified or module-qualified access: std.name or module.name
            if (fe->object->kind == ASTNode::Identifier) {
                auto* ident = static_cast<IdentifierExpr*>(fe->object.get());

                auto modIt = importedModules_.find(ident->name);
                if (modIt != importedModules_.end()) {
                    ModuleInfo* mod = modIt->second;
                    auto expIt = mod->exports.find(fe->field);
                    if (expIt == mod->exports.end()) {
                        error(expr->loc, "Module '" + ident->name +
                              "' does not export '" + fe->field + "'");
                        result = compiler_.intType();
                        break;
                    }
                    const ExportEntry& entry = expIt->second;
                    switch (entry.kind) {
                        case ExportEntry::Var:
                            result = entry.type;
                            // Tag the expr with the global index for codegen
                            fe->resolvedType = entry.type;
                            break;
                        case ExportEntry::Func:
                            // For a bare function reference (not a call), use the first overload's type
                            if (!entry.funcOverloads.empty()) {
                                result = entry.funcOverloads[0].returnType;
                            } else {
                                result = compiler_.voidType();
                            }
                            break;
                        case ExportEntry::StructT:
                            result = entry.structType;
                            break;
                        case ExportEntry::EnumT:
                            result = entry.enumType;
                            break;
                        case ExportEntry::TemplateStructT:
                        case ExportEntry::TemplateEnumT:
                            error(expr->loc, "Template type '" + fe->field +
                                  "' requires type arguments");
                            result = compiler_.intType();
                            break;
                        case ExportEntry::TypeAlias:
                            result = entry.aliasType;
                            break;
                        case ExportEntry::TemplateTypeAlias:
                            error(expr->loc, "Generic type alias '" + fe->field +
                                  "' requires type arguments");
                            result = compiler_.intType();
                            break;
                        case ExportEntry::ConstraintT:
                            error(expr->loc, "'" + fe->field + "' is a constraint, not a value");
                            result = compiler_.intType();
                            break;
                        case ExportEntry::ModuleAlias:
                            // Chained module access (A.math.sin) is not
                            // supported. Users should wildcard- or
                            // named-import the re-exporter instead.
                            error(expr->loc, "Re-exported module '" + fe->field +
                                  "' cannot be accessed through a whole-module import of '" +
                                  ident->name + "'. Use 'import " + ident->name +
                                  ".*' or 'import " + ident->name + ".{" + fe->field + "}'.");
                            result = compiler_.intType();
                            break;
                    }
                    break;
                }
            }

            // Check for enum no-data case construction: EnumName.caseName
            if (fe->object->kind == ASTNode::Identifier) {
                auto* ident = static_cast<IdentifierExpr*>(fe->object.get());
                auto enumIt = enumTypes_.find(ident->name);
                if (enumIt != enumTypes_.end()) {
                    EnumType* etype = enumIt->second;
                    // Find the case
                    bool found = false;
                    for (size_t i = 0; i < etype->cases_.size(); ++i) {
                        if (etype->cases_[i].name->str() == fe->field) {
                            found = true;
                            if (etype->cases_[i].type != compiler_.voidType()) {
                                error(expr->loc, "Enum case '" + fe->field + "' requires a value");
                            }
                            break;
                        }
                    }
                    if (!found) {
                        error(expr->loc, "Unknown case '" + fe->field +
                              "' in enum '" + ident->name + "'");
                    }
                    // Re-tag this node as EnumConstructor for codegen
                    expr->kind = ASTNode::EnumConstructor;
                    result = etype;
                    break;
                }
            }

            // Check for explicit @ on the object
            AutoMapArg objAutoMap = extractAutoMapAnnotation(static_cast<Expr*>(fe->object.get()));

            Type* objType = inferExpr(static_cast<Expr*>(fe->object.get()));
            if (!objType) {
                result = compiler_.intType();
                break;
            }

            // Explicit @ auto-map on field access: array @ .field
            if (objAutoMap) {
                bool isList = false;
                Type* innerType = unwrapAutoMapLayers(objType, objAutoMap.depth, isList, fe->object->loc);
                objAutoMap.isList = isList;

                // Resolve field on inner type
                Type* fieldType = nullptr;
                if (auto* stype = dynamic_cast<StructType*>(innerType)) {
                    for (size_t i = 0; i < stype->fields_.size(); ++i) {
                        if (stype->fields_[i].name->str() == fe->field) {
                            fieldType = stype->fields_[i].type;
                            break;
                        }
                    }
                    if (!fieldType) {
                        error(expr->loc, "Struct '" + std::string(stype->name_->str()) +
                              "' has no field '" + fe->field + "'");
                        result = compiler_.intType();
                        break;
                    }
                } else if (auto* ttype = dynamic_cast<TupleType*>(innerType)) {
                    try {
                        size_t idx = std::stoul(fe->field);
                        if (idx < ttype->fields_.size()) {
                            fieldType = ttype->fields_[idx];
                        } else {
                            error(expr->loc, "Tuple index " + fe->field + " out of bounds");
                            result = compiler_.intType();
                            break;
                        }
                    } catch (...) {
                        error(expr->loc, "Tuples can only be accessed by numeric index");
                        result = compiler_.intType();
                        break;
                    }
                } else {
                    error(expr->loc, "Explicit '@' on field access requires Array/List of structs or tuples");
                    result = compiler_.intType();
                    break;
                }

                fe->autoMap = objAutoMap;
                // Wrap field type in the same container layers
                result = fieldType;
                for (int d = 0; d < objAutoMap.depth; ++d) {
                    result = isList ? static_cast<Type*>(compiler_.listType(result))
                                    : static_cast<Type*>(compiler_.arrayType(result));
                }
                break;
            }

            // Implicit auto-mapping: peel Array/List layers to find Struct/Tuple
            {
                Type* t = objType;
                int depth = 0;
                bool isList = false;
                while (true) {
                    if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
                        t = arrT->elemType_;
                        depth++;
                    } else if (auto* listT = dynamic_cast<ListType*>(t)) {
                        t = listT->elemType_;
                        depth++;
                        isList = true;
                    } else {
                        break;
                    }
                }
                if (depth > 0 && (dynamic_cast<StructType*>(t) || dynamic_cast<TupleType*>(t))) {
                    // Resolve field on the inner struct/tuple type
                    Type* fieldType = nullptr;
                    if (auto* stype = dynamic_cast<StructType*>(t)) {
                        for (size_t i = 0; i < stype->fields_.size(); ++i) {
                            if (stype->fields_[i].name->str() == fe->field) {
                                fieldType = stype->fields_[i].type;
                                break;
                            }
                        }
                        if (!fieldType) {
                            error(expr->loc, "Struct '" + std::string(stype->name_->str()) +
                                  "' has no field '" + fe->field + "'");
                            result = compiler_.intType();
                            break;
                        }
                    } else if (auto* ttype = dynamic_cast<TupleType*>(t)) {
                        try {
                            size_t idx = std::stoul(fe->field);
                            if (idx < ttype->fields_.size()) {
                                fieldType = ttype->fields_[idx];
                            } else {
                                error(expr->loc, "Tuple index " + fe->field + " out of bounds");
                                result = compiler_.intType();
                                break;
                            }
                        } catch (...) {
                            error(expr->loc, "Tuples can only be accessed by numeric index");
                            result = compiler_.intType();
                            break;
                        }
                    }

                    fe->autoMap = AutoMapArg{depth, 0, isList};
                    // Wrap field type preserving container types at each level
                    // Walk objType again to rebuild the same nesting structure
                    result = fieldType;
                    Type* wrapper = objType;
                    // Collect container types from outer to inner
                    std::vector<bool> isListAtLevel;
                    for (int d = 0; d < depth; ++d) {
                        if (auto* arrT = dynamic_cast<ArrayType*>(wrapper)) {
                            isListAtLevel.push_back(false);
                            wrapper = arrT->elemType_;
                        } else if (auto* listT = dynamic_cast<ListType*>(wrapper)) {
                            isListAtLevel.push_back(true);
                            wrapper = listT->elemType_;
                        }
                    }
                    // Wrap from innermost to outermost
                    for (int d = depth - 1; d >= 0; --d) {
                        result = isListAtLevel[d] ? static_cast<Type*>(compiler_.listType(result))
                                                  : static_cast<Type*>(compiler_.arrayType(result));
                    }
                    break;
                }
            }

            // Handle struct field access
            if (auto* stype = dynamic_cast<StructType*>(objType)) {
                for (size_t i = 0; i < stype->fields_.size(); ++i) {
                    if (stype->fields_[i].name->str() == fe->field) {
                        result = stype->fields_[i].type;
                        break;
                    }
                }
                // For tuple structs, allow numeric field access (e.g., p.0, p.1)
                if (!result && stype->isTupleStruct_) {
                    try {
                        size_t idx = std::stoul(fe->field);
                        if (idx < stype->fields_.size()) {
                            result = stype->fields_[idx].type;
                        } else {
                            error(expr->loc, "Tuple struct field index " + fe->field + " out of bounds");
                            result = compiler_.intType();
                        }
                    } catch (...) {
                        error(expr->loc, "Struct '" + std::string(stype->name_->str()) +
                              "' has no field '" + fe->field + "'");
                        result = compiler_.intType();
                    }
                } else if (!result) {
                    error(expr->loc, "Struct '" + std::string(stype->name_->str()) +
                          "' has no field '" + fe->field + "'");
                    result = compiler_.intType();
                }
                break;
            }

            // Handle tuple field access (e.g., tuple.0, tuple.1)
            if (auto* ttype = dynamic_cast<TupleType*>(objType)) {
                // Try parsing field as index
                try {
                    size_t idx = std::stoul(fe->field);
                    if (idx < ttype->fields_.size()) {
                        result = ttype->fields_[idx];
                    } else {
                        error(expr->loc, "Tuple index " + fe->field + " out of bounds");
                        result = compiler_.intType();
                    }
                } catch (...) {
                    error(expr->loc, "Tuples can only be accessed by numeric index");
                    result = compiler_.intType();
                }
                break;
            }

            error(expr->loc, "Field access not supported on this type");
            result = compiler_.intType();
            break;
        }

        case ASTNode::IndexExpr: {
            auto* ie = static_cast<IndexExpr_*>(expr);

            // Check for explicit @ on the object
            AutoMapArg objAutoMap = extractAutoMapAnnotation(static_cast<Expr*>(ie->object.get()));

            Type* objType = inferExpr(static_cast<Expr*>(ie->object.get()));
            Type* idxType = inferExpr(static_cast<Expr*>(ie->index.get()));

            // A. Explicit @ on object: arr @ [i] or arr @ [[i1,i2,...]]
            if (objAutoMap) {
                bool isList = false;
                Type* innerType = unwrapAutoMapLayers(objType, objAutoMap.depth, isList, ie->object->loc);
                objAutoMap.isList = isList;

                // Resolve indexing on inner type, also handling Array/List index
                Type* elemResult = nullptr;

                // Check if index is Array of indices (combined @ + index automap)
                if (auto* idxArrType = dynamic_cast<ArrayType*>(idxType)) {
                    if (auto* arrType = dynamic_cast<ArrayType*>(innerType)) {
                        if (idxArrType->elemType_ != compiler_.intType()) {
                            error(ie->index->loc, "Array index array must contain Int");
                        }
                        ie->indexAutoMap = AutoMapArg{1, 0, false};
                        elemResult = static_cast<Type*>(compiler_.arrayType(arrType->elemType_));
                    } else if (auto* mapType = dynamic_cast<MapType*>(innerType)) {
                        if (idxArrType->elemType_ != mapType->keyType_) {
                            if (!(isNumeric(idxArrType->elemType_) && isNumeric(mapType->keyType_))) {
                                error(ie->index->loc, "Map index array key type mismatch");
                            }
                        }
                        ie->indexAutoMap = AutoMapArg{1, 0, false};
                        elemResult = static_cast<Type*>(compiler_.arrayType(compiler_.optionType(mapType->valueType_)));
                    } else if (innerType == compiler_.stringType()) {
                        if (idxArrType->elemType_ != compiler_.intType()) {
                            error(ie->index->loc, "String index array must contain Int");
                        }
                        ie->indexAutoMap = AutoMapArg{1, 0, false};
                        elemResult = static_cast<Type*>(compiler_.arrayType(compiler_.intType()));
                    }
                }
                // Check if index is List of indices
                if (!elemResult) if (auto* idxListType = dynamic_cast<ListType*>(idxType)) {
                    if (auto* arrType = dynamic_cast<ArrayType*>(innerType)) {
                        if (idxListType->elemType_ != compiler_.intType()) {
                            error(ie->index->loc, "Array index list must contain Int");
                        }
                        ie->indexAutoMap = AutoMapArg{1, 0, true};
                        elemResult = static_cast<Type*>(compiler_.listType(arrType->elemType_));
                    } else if (auto* mapType = dynamic_cast<MapType*>(innerType)) {
                        if (idxListType->elemType_ != mapType->keyType_) {
                            if (!(isNumeric(idxListType->elemType_) && isNumeric(mapType->keyType_))) {
                                error(ie->index->loc, "Map index list key type mismatch");
                            }
                        }
                        ie->indexAutoMap = AutoMapArg{1, 0, true};
                        elemResult = static_cast<Type*>(compiler_.listType(compiler_.optionType(mapType->valueType_)));
                    } else if (innerType == compiler_.stringType()) {
                        if (idxListType->elemType_ != compiler_.intType()) {
                            error(ie->index->loc, "String index list must contain Int");
                        }
                        ie->indexAutoMap = AutoMapArg{1, 0, true};
                        elemResult = static_cast<Type*>(compiler_.listType(compiler_.intType()));
                    }
                }
                // Scalar indexing
                if (!elemResult) {
                    if (auto* arrType = dynamic_cast<ArrayType*>(innerType)) {
                        if (idxType && idxType != compiler_.intType()) {
                            error(ie->index->loc, "Array index must be Int");
                        }
                        elemResult = arrType->elemType_;
                    } else if (auto* mapType = dynamic_cast<MapType*>(innerType)) {
                        if (idxType && idxType != mapType->keyType_) {
                            if (!(isNumeric(idxType) && isNumeric(mapType->keyType_))) {
                                error(ie->index->loc, "Map index type mismatch");
                            }
                        }
                        elemResult = compiler_.optionType(mapType->valueType_);
                    } else if (innerType == compiler_.stringType()) {
                        if (idxType && idxType != compiler_.intType()) {
                            error(ie->index->loc, "String index must be Int");
                        }
                        elemResult = compiler_.intType();
                    } else {
                        error(expr->loc, "Explicit '@' on index access requires Array/List of indexable types");
                        result = compiler_.intType();
                        break;
                    }
                }

                ie->autoMap = objAutoMap;
                // Wrap result type in the same container layers
                result = elemResult;
                for (int d = 0; d < objAutoMap.depth; ++d) {
                    result = isList ? static_cast<Type*>(compiler_.listType(result))
                                    : static_cast<Type*>(compiler_.arrayType(result));
                }
                break;
            }

            // B. Index is Array/List of indices: arr[[3,1,2,0]]
            if (auto* idxArrType = dynamic_cast<ArrayType*>(idxType)) {
                // Array of indices/keys
                if (auto* arrType = dynamic_cast<ArrayType*>(objType)) {
                    if (idxArrType->elemType_ != compiler_.intType()) {
                        error(ie->index->loc, "Array index array must contain Int");
                    }
                    ie->indexAutoMap = AutoMapArg{1, 0, false};
                    result = compiler_.arrayType(arrType->elemType_);
                } else if (auto* mapType = dynamic_cast<MapType*>(objType)) {
                    if (idxArrType->elemType_ != mapType->keyType_) {
                        if (!(isNumeric(idxArrType->elemType_) && isNumeric(mapType->keyType_))) {
                            error(ie->index->loc, "Map index array key type mismatch");
                        }
                    }
                    ie->indexAutoMap = AutoMapArg{1, 0, false};
                    result = compiler_.arrayType(compiler_.optionType(mapType->valueType_));
                } else if (objType == compiler_.stringType()) {
                    if (idxArrType->elemType_ != compiler_.intType()) {
                        error(ie->index->loc, "String index array must contain Int");
                    }
                    ie->indexAutoMap = AutoMapArg{1, 0, false};
                    result = compiler_.arrayType(compiler_.intType());
                } else {
                    error(expr->loc, "Indexing requires an Array, Map, or String type");
                    result = compiler_.intType();
                }
                break;
            }
            if (auto* idxListType = dynamic_cast<ListType*>(idxType)) {
                // List of indices/keys
                if (auto* arrType = dynamic_cast<ArrayType*>(objType)) {
                    if (idxListType->elemType_ != compiler_.intType()) {
                        error(ie->index->loc, "Array index list must contain Int");
                    }
                    ie->indexAutoMap = AutoMapArg{1, 0, true};
                    result = compiler_.listType(arrType->elemType_);
                } else if (auto* mapType = dynamic_cast<MapType*>(objType)) {
                    if (idxListType->elemType_ != mapType->keyType_) {
                        if (!(isNumeric(idxListType->elemType_) && isNumeric(mapType->keyType_))) {
                            error(ie->index->loc, "Map index list key type mismatch");
                        }
                    }
                    ie->indexAutoMap = AutoMapArg{1, 0, true};
                    result = compiler_.listType(compiler_.optionType(mapType->valueType_));
                } else if (objType == compiler_.stringType()) {
                    if (idxListType->elemType_ != compiler_.intType()) {
                        error(ie->index->loc, "String index list must contain Int");
                    }
                    ie->indexAutoMap = AutoMapArg{1, 0, true};
                    result = compiler_.listType(compiler_.intType());
                } else {
                    error(expr->loc, "Indexing requires an Array, Map, or String type");
                    result = compiler_.intType();
                }
                break;
            }

            // C. Normal indexing (no auto-mapping)
            if (auto* arrType = dynamic_cast<ArrayType*>(objType)) {
                if (idxType && idxType != compiler_.intType()) {
                    error(ie->index->loc, "Array index must be Int");
                }
                result = arrType->elemType_;
            } else if (auto* mapType = dynamic_cast<MapType*>(objType)) {
                if (idxType && idxType != mapType->keyType_) {
                    if (isNumeric(idxType) && isNumeric(mapType->keyType_)) {
                        // numeric promotion is ok
                    } else {
                        error(ie->index->loc, "Map index type mismatch");
                    }
                }
                result = compiler_.optionType(mapType->valueType_);
            } else if (objType == compiler_.stringType()) {
                if (idxType && idxType != compiler_.intType()) {
                    error(ie->index->loc, "String index must be Int");
                }
                result = compiler_.intType();
            } else {
                error(expr->loc, "Indexing requires an Array, Map, or String type");
                result = compiler_.intType();
            }
            break;
        }

        default:
            error(expr->loc, "Cannot infer type of expression");
            result = compiler_.intType();
            break;
    }

    expr->resolvedType = result;
    return result;
}

} // namespace ts
