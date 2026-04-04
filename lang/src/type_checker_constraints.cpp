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
//  type_checker_constraints.cpp
//  lang
//
//  Type checker -- constraint checking
//

#include "type_checker.hpp"
#include "module_compiler.hpp"
#include "builtins.hpp"
#include "value.hpp"
#include "diagnostic.hpp"

namespace ts {

// --- Constraint checking ---

// Desugar constraint names used directly as parameter types into fresh type params.
// e.g. fn foo(a AsSignal, b AsSignal) Signal
//   => fn foo<__T0: AsSignal, __T1: AsSignal>(a __T0, b __T1) Signal
void TypeChecker::desugarConstraintParams(FnDeclNode* decl) {
    static const std::set<std::string> builtinTypeNames = {
        "Int", "Float", "String", "Bool", "Symbol", "Void", "Fraction", "Complex", "Any"
    };

    int genCounter = 0;

    // Recursive helper: walk a type expression tree, replacing constraint
    // names with fresh type parameters and adding where constraints.
    std::function<void(TypeExpr*)> walkTypeExpr = [&](TypeExpr* expr) {
        if (!expr) return;
        switch (expr->kind) {
        case ASTNode::NamedType: {
            auto* named = static_cast<NamedTypeNode*>(expr);
            if (constraints_.count(named->name) == 0) break;
            if (builtinTypeNames.count(named->name)) break;
            if (structTypes_.count(named->name)) break;
            if (enumTypes_.count(named->name)) break;
            if (typeAliases_.count(named->name)) break;
            bool isTypeParam = false;
            for (auto& tp : decl->typeParams) {
                if (tp == named->name) { isTypeParam = true; break; }
            }
            if (isTypeParam) break;

            std::string constraintName = named->name;
            std::string typeParamName = "__T" + std::to_string(genCounter++);
            decl->typeParams.push_back(typeParamName);
            decl->whereConstraints.push_back(
                WhereConstraint{typeParamName, constraintName, expr->loc});
            named->name = typeParamName;
            break;
        }
        case ASTNode::ArrayType:
            walkTypeExpr(static_cast<ArrayTypeNode*>(expr)->elemType.get());
            break;
        case ASTNode::ListType:
            walkTypeExpr(static_cast<ListTypeNode*>(expr)->elemType.get());
            break;
        case ASTNode::SetType:
            walkTypeExpr(static_cast<SetTypeNode*>(expr)->elemType.get());
            break;
        case ASTNode::RefType:
            walkTypeExpr(static_cast<RefTypeNode*>(expr)->elemType.get());
            break;
        case ASTNode::MapType: {
            auto* m = static_cast<MapTypeNode*>(expr);
            walkTypeExpr(m->keyType.get());
            walkTypeExpr(m->valueType.get());
            break;
        }
        case ASTNode::TupleType:
            for (auto& e : static_cast<TupleTypeNode*>(expr)->elemTypes)
                walkTypeExpr(e.get());
            break;
        case ASTNode::FunctionType: {
            auto* f = static_cast<FunctionTypeNode*>(expr);
            for (auto& p : f->paramTypes) walkTypeExpr(p.get());
            walkTypeExpr(f->returnType.get());
            break;
        }
        case ASTNode::TemplateType:
            for (auto& a : static_cast<TemplateTypeNode*>(expr)->typeArgs)
                walkTypeExpr(a.get());
            break;
        default:
            break;
        }
    };

    for (auto& param : decl->params) {
        if (!param.typeExpr) continue;
        walkTypeExpr(param.typeExpr.get());
    }

    // Check if the return type contains constraint references.
    // If so, save it as a constraint to be validated after return type inference.
    if (decl->returnType) {
        bool hasConstraintRef = false;
        std::function<bool(TypeExpr*)> checkForConstraints = [&](TypeExpr* expr) -> bool {
            if (!expr) return false;
            if (expr->kind == ASTNode::NamedType) {
                auto* named = static_cast<NamedTypeNode*>(expr);
                if (constraints_.count(named->name) &&
                    !builtinTypeNames.count(named->name) &&
                    !structTypes_.count(named->name) &&
                    !enumTypes_.count(named->name) &&
                    !typeAliases_.count(named->name)) {
                    bool isTP = false;
                    for (auto& tp : decl->typeParams) {
                        if (tp == named->name) { isTP = true; break; }
                    }
                    if (!isTP) return true;
                }
                return false;
            }
            switch (expr->kind) {
            case ASTNode::ArrayType:
                return checkForConstraints(static_cast<ArrayTypeNode*>(expr)->elemType.get());
            case ASTNode::ListType:
                return checkForConstraints(static_cast<ListTypeNode*>(expr)->elemType.get());
            case ASTNode::SetType:
                return checkForConstraints(static_cast<SetTypeNode*>(expr)->elemType.get());
            case ASTNode::RefType:
                return checkForConstraints(static_cast<RefTypeNode*>(expr)->elemType.get());
            case ASTNode::MapType: {
                auto* m = static_cast<MapTypeNode*>(expr);
                return checkForConstraints(m->keyType.get()) || checkForConstraints(m->valueType.get());
            }
            case ASTNode::TupleType:
                for (auto& e : static_cast<TupleTypeNode*>(expr)->elemTypes)
                    if (checkForConstraints(e.get())) return true;
                return false;
            case ASTNode::FunctionType: {
                auto* f = static_cast<FunctionTypeNode*>(expr);
                for (auto& p : f->paramTypes)
                    if (checkForConstraints(p.get())) return true;
                return checkForConstraints(f->returnType.get());
            }
            case ASTNode::TemplateType:
                for (auto& a : static_cast<TemplateTypeNode*>(expr)->typeArgs)
                    if (checkForConstraints(a.get())) return true;
                return false;
            default:
                return false;
            }
        };
        hasConstraintRef = checkForConstraints(decl->returnType.get());
        if (hasConstraintRef) {
            decl->returnTypeConstraint = std::move(decl->returnType);
            decl->returnType = nullptr;
        }
    }
}

void TypeChecker::checkConstraintDecl(ConstraintDeclNode* decl) {
    if (constraints_.count(decl->name)) {
        error(decl->loc, "Duplicate constraint name '" + decl->name + "'");
        return;
    }

    // Pre-register the constraint name (empty info) so self-references work
    ConstraintInfo info;
    info.name = decl->name;
    info.typeParams = decl->typeParams;
    info.declNode = decl;
    constraints_[decl->name] = std::move(info);

    ConstraintInfo& infoRef = constraints_[decl->name];

    // Union items form: build constraint patterns
    if (!decl->items.empty()) {
        for (auto& typeExpr : decl->items) {
            infoRef.patterns.push_back(buildConstraintPattern(typeExpr.get()));
        }
    }

    // Structural form: store references to AST type exprs (resolved at check time)
    if (!decl->requiredFns.empty()) {
        for (auto& sig : decl->requiredFns) {
            ConstraintInfo::ReqFn reqFn;
            reqFn.name = sig.name;
            for (auto& pt : sig.paramTypes) {
                reqFn.paramTypeExprs.push_back(&pt);
            }
            reqFn.returnTypeExpr = &sig.returnType;
            infoRef.requiredFns.push_back(std::move(reqFn));
        }
    }

    // Composition form: store references to component constraints
    if (!decl->components.empty()) {
        for (auto& comp : decl->components) {
            ConstraintInfo::ComponentRef ref;
            ref.name = comp.name;
            for (auto& ta : comp.typeArgs) {
                ref.typeArgExprs.push_back(&ta);
            }
            infoRef.components.push_back(std::move(ref));
        }
    }
}

TypeChecker::ConstraintPattern TypeChecker::buildConstraintPattern(TypeExpr* expr) {
    ConstraintPattern pat;

    if (expr->kind == ASTNode::NamedType) {
        auto* named = static_cast<NamedTypeNode*>(expr);
        // Is it a known constraint or the constraint being defined?
        if (constraints_.count(named->name)) {
            pat.kind = ConstraintPattern::ConstraintRef;
            pat.constraintName = named->name;
            return pat;
        }
        // Otherwise resolve as concrete type
        pat.kind = ConstraintPattern::ConcreteType;
        pat.type = resolveTypeExpr(expr);
        return pat;
    }

    // Parameterized container types: [T], List[T], {K:V}, etc.
    if (expr->kind == ASTNode::ArrayType) {
        auto* arr = static_cast<ArrayTypeNode*>(expr);
        auto elemPat = buildConstraintPattern(arr->elemType.get());
        if (elemPat.kind == ConstraintPattern::ConcreteType) {
            // Pure concrete — resolve as concrete type
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::Array;
        pat.args.push_back(std::move(elemPat));
        return pat;
    }

    if (expr->kind == ASTNode::ListType) {
        auto* lst = static_cast<ListTypeNode*>(expr);
        auto elemPat = buildConstraintPattern(lst->elemType.get());
        if (elemPat.kind == ConstraintPattern::ConcreteType) {
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::List;
        pat.args.push_back(std::move(elemPat));
        return pat;
    }

    if (expr->kind == ASTNode::SetType) {
        auto* s = static_cast<SetTypeNode*>(expr);
        auto elemPat = buildConstraintPattern(s->elemType.get());
        if (elemPat.kind == ConstraintPattern::ConcreteType) {
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::Set;
        pat.args.push_back(std::move(elemPat));
        return pat;
    }

    if (expr->kind == ASTNode::RefType) {
        auto* r = static_cast<RefTypeNode*>(expr);
        auto elemPat = buildConstraintPattern(r->elemType.get());
        if (elemPat.kind == ConstraintPattern::ConcreteType) {
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::Ref;
        pat.args.push_back(std::move(elemPat));
        return pat;
    }

    if (expr->kind == ASTNode::MapType) {
        auto* m = static_cast<MapTypeNode*>(expr);
        auto keyPat = buildConstraintPattern(m->keyType.get());
        auto valPat = buildConstraintPattern(m->valueType.get());
        if (keyPat.kind == ConstraintPattern::ConcreteType &&
            valPat.kind == ConstraintPattern::ConcreteType) {
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::Map;
        pat.args.push_back(std::move(keyPat));
        pat.args.push_back(std::move(valPat));
        return pat;
    }

    if (expr->kind == ASTNode::TupleType) {
        auto* tup = static_cast<TupleTypeNode*>(expr);
        std::vector<ConstraintPattern> elemPats;
        bool allConcrete = true;
        for (auto& elem : tup->elemTypes) {
            elemPats.push_back(buildConstraintPattern(elem.get()));
            if (elemPats.back().kind != ConstraintPattern::ConcreteType)
                allConcrete = false;
        }
        if (allConcrete) {
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::Tuple;
        pat.args = std::move(elemPats);
        return pat;
    }

    if (expr->kind == ASTNode::FunctionType) {
        auto* fn = static_cast<FunctionTypeNode*>(expr);
        std::vector<ConstraintPattern> argPats;
        bool allConcrete = true;
        for (auto& param : fn->paramTypes) {
            argPats.push_back(buildConstraintPattern(param.get()));
            if (argPats.back().kind != ConstraintPattern::ConcreteType)
                allConcrete = false;
        }
        auto retPat = buildConstraintPattern(fn->returnType.get());
        if (retPat.kind != ConstraintPattern::ConcreteType)
            allConcrete = false;
        if (allConcrete) {
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::Function;
        pat.args = std::move(argPats);
        pat.args.push_back(std::move(retPat));  // return type is last
        return pat;
    }

    // TemplateType: user-defined parameterized types like Pair<T>
    if (expr->kind == ASTNode::TemplateType) {
        auto* tmpl = static_cast<TemplateTypeNode*>(expr);
        // Check if template name itself is a known constraint
        if (constraints_.count(tmpl->name)) {
            // Not supported as parameterized constraint pattern for now
            error(expr->loc, "Parameterized constraint references not supported in union items");
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = compiler_.intType();
            return pat;
        }
        // Recursively process type args -- they may contain constraint refs
        std::vector<ConstraintPattern> argPats;
        bool allConcrete = true;
        for (auto& arg : tmpl->typeArgs) {
            argPats.push_back(buildConstraintPattern(arg.get()));
            if (argPats.back().kind != ConstraintPattern::ConcreteType)
                allConcrete = false;
        }
        if (allConcrete) {
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::Template;
        pat.templateName = tmpl->name;
        pat.args = std::move(argPats);
        return pat;
    }

    // All other type expressions: resolve as concrete type
    pat.kind = ConstraintPattern::ConcreteType;
    pat.type = resolveTypeExpr(expr);
    return pat;
}

bool TypeChecker::matchConstraintPattern(Type* concrete, const ConstraintPattern& pattern) {
    switch (pattern.kind) {
        case ConstraintPattern::ConcreteType:
            return concrete == pattern.type;

        case ConstraintPattern::ConstraintRef:
            return checkConstraint(concrete, pattern.constraintName, "", "", {}, false);

        case ConstraintPattern::Parameterized: {
            // Decompose concrete type and match constructor + args
            switch (pattern.ctor) {
                case ConstraintPattern::Ctor::Array: {
                    auto* at = dynamic_cast<ArrayType*>(concrete);
                    if (!at || pattern.args.size() != 1) return false;
                    return matchConstraintPattern(at->elemType_, pattern.args[0]);
                }
                case ConstraintPattern::Ctor::List: {
                    auto* lt = dynamic_cast<ListType*>(concrete);
                    if (!lt || pattern.args.size() != 1) return false;
                    return matchConstraintPattern(lt->elemType_, pattern.args[0]);
                }
                case ConstraintPattern::Ctor::Set: {
                    auto* st = dynamic_cast<SetType*>(concrete);
                    if (!st || pattern.args.size() != 1) return false;
                    return matchConstraintPattern(st->elemType_, pattern.args[0]);
                }
                case ConstraintPattern::Ctor::Ref: {
                    auto* rt = dynamic_cast<RefType*>(concrete);
                    if (!rt || pattern.args.size() != 1) return false;
                    return matchConstraintPattern(rt->elemType_, pattern.args[0]);
                }
                case ConstraintPattern::Ctor::Range: {
                    auto* rng = dynamic_cast<RangeType*>(concrete);
                    if (!rng || pattern.args.size() != 1) return false;
                    return matchConstraintPattern(rng->elemType_, pattern.args[0]);
                }
                case ConstraintPattern::Ctor::Coroutine: {
                    auto* co = dynamic_cast<CoroutineType*>(concrete);
                    if (!co || pattern.args.size() != 1) return false;
                    return matchConstraintPattern(co->yieldType_, pattern.args[0]);
                }
                case ConstraintPattern::Ctor::Map: {
                    auto* mt = dynamic_cast<MapType*>(concrete);
                    if (!mt || pattern.args.size() != 2) return false;
                    return matchConstraintPattern(mt->keyType_, pattern.args[0]) &&
                           matchConstraintPattern(mt->valueType_, pattern.args[1]);
                }
                case ConstraintPattern::Ctor::Tuple: {
                    auto* tt = dynamic_cast<TupleType*>(concrete);
                    if (!tt || tt->fields_.size() != pattern.args.size()) return false;
                    for (size_t i = 0; i < pattern.args.size(); ++i) {
                        if (!matchConstraintPattern(tt->fields_[i], pattern.args[i]))
                            return false;
                    }
                    return true;
                }
                case ConstraintPattern::Ctor::Function: {
                    auto* ft = dynamic_cast<FunctionType*>(concrete);
                    if (!ft) return false;
                    // args layout: [param0, param1, ..., returnType]
                    size_t numParams = pattern.args.size() - 1;
                    if (ft->argTypes_.size() != numParams) return false;
                    for (size_t i = 0; i < numParams; ++i) {
                        if (!matchConstraintPattern(ft->argTypes_[i], pattern.args[i]))
                            return false;
                    }
                    return matchConstraintPattern(ft->returnType_, pattern.args.back());
                }
                case ConstraintPattern::Ctor::Template: {
                    // Look up the concrete type's monomorphization origin
                    auto it = monoOrigin_.find(concrete);
                    if (it == monoOrigin_.end()) return false;
                    auto& origin = it->second;
                    if (origin.templateName != pattern.templateName) return false;
                    if (origin.typeArgs.size() != pattern.args.size()) return false;
                    for (size_t i = 0; i < pattern.args.size(); ++i) {
                        if (!matchConstraintPattern(origin.typeArgs[i], pattern.args[i]))
                            return false;
                    }
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

bool TypeChecker::hasBuiltinOperator(const std::string& opName, Type* lhs, Type* rhs) {
    // Check if a binary operator is built-in for these types
    bool lhsNumeric = isNumeric(lhs);
    bool rhsNumeric = isNumeric(rhs);
    bool bothNumeric = lhsNumeric && rhsNumeric;
    bool bothInt = (lhs == compiler_.intType() || lhs == compiler_.boolType()) &&
                   (rhs == compiler_.intType() || rhs == compiler_.boolType());
    bool bothString = lhs == compiler_.stringType() && rhs == compiler_.stringType();

    if (opName == "+" || opName == "-" || opName == "*" || opName == "/") {
        return bothNumeric;
    }
    if (opName == "%" || opName == "//") {
        return bothInt;
    }
    if (opName == "==" || opName == "!=") {
        return bothNumeric || bothString;
    }
    if (opName == "<" || opName == "<=" || opName == ">" || opName == ">=") {
        if (bothString) return true;
        if (bothNumeric) {
            // Complex numbers don't support ordering
            return lhs != compiler_.complexType() && rhs != compiler_.complexType();
        }
        return false;
    }
    if (opName == "&" || opName == "|" || opName == "^" || opName == "<<" || opName == ">>" || opName == ">>>") {
        return lhs == compiler_.intType() && rhs == compiler_.intType();
    }
    if (opName == "&&" || opName == "||") {
        return true; // built-in for all types
    }
    return false;
}

bool TypeChecker::checkConstraint(Type* concreteType, const std::string& constraintName,
                                   const std::string& typeParamName, const std::string& contextName,
                                   SourceRange loc, bool emitError) {
    auto it = constraints_.find(constraintName);
    if (it == constraints_.end()) {
        if (emitError) {
            error(loc, "Unknown constraint '" + constraintName + "'");
        }
        return false;
    }

    // Recursion guard: if we're already checking this (type, constraint) pair, return false
    auto guardKey = std::make_pair(concreteType, constraintName);
    if (constraintCheckStack_.count(guardKey)) {
        return false;
    }
    constraintCheckStack_.insert(guardKey);

    // RAII cleanup for the recursion guard
    struct GuardCleanup {
        std::set<std::pair<Type*, std::string>>& stack;
        std::pair<Type*, std::string> key;
        ~GuardCleanup() { stack.erase(key); }
    } cleanup{constraintCheckStack_, guardKey};

    const ConstraintInfo& info = it->second;

    // Pattern-based constraint: check if concreteType matches any pattern (union semantics)
    if (!info.patterns.empty()) {
        for (const auto& pat : info.patterns) {
            if (matchConstraintPattern(concreteType, pat)) return true;
        }
        if (emitError) {
            auto ts = concreteType->str();
            error(loc, "Type '" + std::string(ts.data(), ts.size()) +
                  "' does not satisfy constraint '" + constraintName +
                  "' (not in type set)");
        }
        return false;
    }

    // Structural constraint: check required functions exist
    if (!info.requiredFns.empty()) {
        // Set up temporary type param bindings for resolving the constraint's type exprs
        auto savedBindings = typeParamBindings_;
        if (!info.typeParams.empty()) {
            typeParamBindings_[info.typeParams[0]] = concreteType;
        }

        bool allOk = true;
        for (auto& reqFn : info.requiredFns) {
            // Resolve param types with the concrete type substituted
            std::vector<Type*> paramTypes;
            for (auto* ptExpr : reqFn.paramTypeExprs) {
                paramTypes.push_back(resolveTypeExpr(ptExpr->get()));
            }
            // Resolve return type (for future use in return type checking)
            (void)resolveTypeExpr(reqFn.returnTypeExpr->get());

            // Check if the function exists via overload resolution
            FuncInfo* func = tryResolveOverload(reqFn.name, paramTypes);
            if (!func) {
                // Also check built-in operators
                if (paramTypes.size() == 2 && hasBuiltinOperator(reqFn.name, paramTypes[0], paramTypes[1])) {
                    // Built-in operator exists
                    continue;
                }
                // Try template resolution too
                func = tryResolveTemplate(reqFn.name, paramTypes, nullptr);
            }
            if (!func) {
                if (emitError) {
                    auto ts = concreteType->str();
                    std::string sigStr = reqFn.name + "(";
                    for (size_t i = 0; i < paramTypes.size(); ++i) {
                        if (i > 0) sigStr += ", ";
                        auto ps = paramTypes[i]->str();
                        sigStr += std::string(ps.data(), ps.size());
                    }
                    sigStr += ")";
                    error(loc, "Type '" + std::string(ts.data(), ts.size()) +
                          "' does not satisfy constraint '" + constraintName +
                          "': missing fn " + sigStr);
                }
                allOk = false;
            }
        }

        typeParamBindings_ = savedBindings;
        return allOk;
    }

    // Composition constraint: check all components
    if (!info.components.empty()) {
        auto savedBindings = typeParamBindings_;
        if (!info.typeParams.empty()) {
            typeParamBindings_[info.typeParams[0]] = concreteType;
        }

        bool allOk = true;
        for (auto& comp : info.components) {
            // Resolve type args for parameterized constraint refs
            // For simple refs (no type args), just pass the concrete type
            if (!checkConstraint(concreteType, comp.name, typeParamName, contextName, loc, emitError)) {
                allOk = false;
            }
        }

        typeParamBindings_ = savedBindings;
        return allOk;
    }

    // Empty constraint (no type-set, no requires, no components) — always satisfied
    return true;
}

bool TypeChecker::checkWhereConstraints(const std::vector<WhereConstraint>& constraints,
                                         const std::unordered_map<std::string, Type*>& bindings,
                                         const std::string& contextName, bool emitError) {
    bool allOk = true;
    for (auto& wc : constraints) {
        auto it = bindings.find(wc.typeParam);
        if (it == bindings.end()) {
            if (emitError) {
                error(wc.loc, "Where clause references unknown type param '" + wc.typeParam + "'");
            }
            allOk = false;
            continue;
        }
        Type* concreteType = it->second;
        if (!checkConstraint(concreteType, wc.constraintName, wc.typeParam, contextName, wc.loc, emitError)) {
            allOk = false;
        }
    }
    return allOk;
}

} // namespace ts
