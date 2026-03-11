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
//  repl_session.cpp
//  lang
//
//  REPLSession implementation — includes all internal headers here only.
//

#include "repl_session.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "type_checker.hpp"
#include "codegen.hpp"
#include "value.hpp"
#include "module_compiler.hpp"
#include "diagnostic.hpp"
#include <cstdio>

namespace ts {

// Convert a VMString (custom allocator) to std::string
static std::string toStdString(const VMString& vs) {
    return std::string(vs.data(), vs.size());
}

struct REPLSession::Impl {
    Compiler& compiler;
    VM& vm;
    VMTarget target;
    ModuleCompiler moduleCompiler;
    TypeChecker typeChecker;
    std::vector<Program> programs;  // keep ASTs alive for template declNodes

    Impl(Compiler& c, VM& v, const VMTarget& t, std::vector<std::string> includePaths)
        : compiler(c), vm(v), target(t),
          moduleCompiler(c, std::move(includePaths)),
          typeChecker(c, &moduleCompiler)
    {
        // TypeChecker constructor registers builtins (creates Primitive objects).
        // This requires compilation context for global allocation.
        compiler.makeCurrent(target);
        auto [initGlobals, initGlobalBase] = compiler.takePendingGlobals();
        compiler.endCurrent();

        // Install builtin globals and switch to VM context
        vm.makeCurrent();
        if (!initGlobals.empty()) {
            CompileResult initResult;
            initResult.newGlobals = std::move(initGlobals);
            initResult.globalBase = initGlobalBase;
            initResult.target = target;
            vm.install(initResult);
        }
    }

    // Install pending globals after compilation
    void installPendingGlobals() {
        u32 numDynVars = compiler.numDynVars();
        auto [newGlobals, globalBase] = compiler.takePendingGlobals();
        compiler.endCurrent();
        vm.makeCurrent();
        {
            CompileResult installResult;
            installResult.newGlobals = std::move(newGlobals);
            installResult.globalBase = globalBase;
            installResult.numDynVars = numDynVars;
            installResult.target = target;
            vm.install(installResult);
        }
    }
};

REPLSession::REPLSession(Compiler& compiler, VM& vm, const VMTarget& target,
                         std::vector<std::string> includePaths)
    : impl_(std::make_unique<Impl>(compiler, vm, target, std::move(includePaths)))
{}

REPLSession::~REPLSession() = default;

REPLSession::EvalResult REPLSession::eval(const std::string& input) {
    EvalResult result;
    result.source = input;

    // Lex + Parse
    Lexer lexer(input, "<repl>");
    Parser parser(lexer);
    impl_->programs.push_back(parser.parseProgram());
    Program& program = impl_->programs.back();

    if (parser.hasErrors()) {
        result.errors = parser.errors();
        impl_->programs.pop_back();
        return result;
    }

    // --- Compilation phase ---
    impl_->compiler.makeCurrent(impl_->target);

    // Type-check (incremental, accumulates state)
    impl_->typeChecker.checkREPLInput(program);
    bool typeError = impl_->typeChecker.hasErrors();

    // Determine if last item is a printable expression
    bool lastIsExpr = false;
    Type* lastType = nullptr;
    CodeBlock* block = nullptr;

    if (!typeError) {
        if (!program.items.empty()) {
            auto* last = program.items.back().get();
            if (last->kind == ASTNode::ExprStmt) {
                lastType = static_cast<ExprStmtNode*>(last)->expr->resolvedType;
                lastIsExpr = (lastType && lastType != impl_->vm.voidType());
            }
        }

        // Codegen
        CodeGen codegen(impl_->compiler, impl_->typeChecker);
        codegen.enableRegReclaim = impl_->compiler.enableRegReclaim;
        codegen.enableConstFold = impl_->compiler.enableConstFold;
        codegen.enableTailCalls = impl_->compiler.enableTailCalls;
        block = lastIsExpr
            ? codegen.generateREPL(program)
            : codegen.generate(program);

        if (codegen.hasErrors()) {
            result.errors = codegen.errors();
            impl_->installPendingGlobals();
            return result;
        }
    }

    // Always harvest and install pending globals
    impl_->installPendingGlobals();

    if (typeError) {
        result.errors = impl_->typeChecker.errors();
        return result;
    }

    // Execute
    try {
        Word value = impl_->vm.execute(block);
        result.success = true;
        if (lastIsExpr) {
            result.hasValue = true;
            VMString vs = wordToString(value, lastType);
            result.formattedValue = toStdString(vs);
            VMString ts = lastType->str();
            result.typeName = toStdString(ts);
        }
    } catch (const std::exception& e) {
        result.errors.push_back(CompileError(
            CompileError::TypeError, SourceRange{}, std::string("Runtime error: ") + e.what()));
    }

    return result;
}

REPLSession::EvalResult REPLSession::queryType(const std::string& expr) {
    EvalResult result;
    result.source = expr;

    // Enter compilation context for type-checking
    impl_->compiler.makeCurrent(impl_->target);

    Lexer lexer(expr, "<repl>");
    Parser parser(lexer);
    Program program = parser.parseProgram();

    if (parser.hasErrors()) {
        impl_->compiler.endCurrent();
        impl_->vm.makeCurrent();
        result.errors = parser.errors();
        return result;
    }

    impl_->typeChecker.checkREPLInput(program);

    // Harvest any globals
    impl_->installPendingGlobals();

    if (impl_->typeChecker.hasErrors()) {
        result.errors = impl_->typeChecker.errors();
        return result;
    }

    result.success = true;
    if (!program.items.empty()) {
        auto* last = program.items.back().get();
        if (last->kind == ASTNode::ExprStmt) {
            Type* t = static_cast<ExprStmtNode*>(last)->expr->resolvedType;
            if (t) {
                VMString ts = t->str();
                result.typeName = toStdString(ts);
                result.hasValue = true;
            }
        }
    }

    return result;
}

std::vector<std::string> REPLSession::listGlobals() const {
    std::vector<std::string> lines;
    auto& globals = impl_->typeChecker.globalVars();
    for (auto& [name, info] : globals) {
        auto typeName = info.type->str();
        char buf[512];
        std::snprintf(buf, sizeof(buf), "  %s %.*s : %.*s",
            info.isMutable ? "var" : "let",
            (int)name.size(), name.data(),
            (int)typeName.size(), typeName.data());
        lines.push_back(buf);
    }
    return lines;
}

std::vector<std::string> REPLSession::listFunctions() const {
    std::vector<std::string> lines;
    auto& funcs = impl_->typeChecker.functions();
    for (auto& [name, overloads] : funcs) {
        for (auto& fi : overloads) {
            if (fi.isBuiltin) continue;
            std::string line = "  fn " + name + "(";
            for (size_t i = 0; i < fi.paramTypes.size(); ++i) {
                if (i > 0) line += ", ";
                auto tn = fi.paramTypes[i]->str();
                line.append(tn.data(), tn.size());
            }
            line += ")";
            if (fi.returnType) {
                auto rn = fi.returnType->str();
                line += " ";
                line.append(rn.data(), rn.size());
            }
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

} // namespace ts
