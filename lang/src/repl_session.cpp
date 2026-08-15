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
#include "pretty_print.hpp"
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
    std::unique_ptr<ModuleCompiler> ownedModuleCompiler;  // null when borrowing
    ModuleCompiler& moduleCompiler;
    TypeChecker typeChecker;
    std::vector<Program> programs;  // keep ASTs alive for template declNodes
    int displayWidth = 80;          // line width for EvalResult::prettyValue
    std::string documentPath;       // anchors document-relative imports

    Impl(Compiler& c, VM& v, const VMTarget& t, std::vector<std::string> includePaths)
        : compiler(c), vm(v), target(t),
          ownedModuleCompiler(std::make_unique<ModuleCompiler>(c, std::move(includePaths))),
          moduleCompiler(*ownedModuleCompiler),
          typeChecker(c, &moduleCompiler)
    {
        initBuiltins();
    }

    Impl(Compiler& c, VM& v, const VMTarget& t, ModuleCompiler& mc)
        : compiler(c), vm(v), target(t),
          moduleCompiler(mc),
          typeChecker(c, &moduleCompiler)
    {
        initBuiltins();
    }

    void initBuiltins() {
        // TypeChecker constructor registers builtins (creates Primitive objects).
        // This requires compilation context for global allocation.
        compiler.makeCurrent(target);
        auto [initCode, initCodeBase] = compiler.takePendingCodeGlobals();
        auto [initData, initDataBase] = compiler.takePendingDataGlobals();
        compiler.endCurrent();

        // Install builtin globals and switch to VM context
        vm.makeCurrent();
        if (!initCode.empty() || !initData.empty()) {
            CompileResult initResult;
            initResult.newCodeGlobals = std::move(initCode);
            initResult.codeBase = initCodeBase;
            initResult.newDataGlobals = std::move(initData);
            initResult.dataBase = initDataBase;
            initResult.target = target;
            vm.install(initResult);
        }
    }

    // Install pending globals after compilation
    void installPendingGlobals() {
        u32 numDynVars = compiler.numDynVars();
        auto [newCode, codeBase] = compiler.takePendingCodeGlobals();
        auto [newData, dataBase] = compiler.takePendingDataGlobals();
        compiler.endCurrent();
        vm.makeCurrent();
        {
            CompileResult installResult;
            installResult.newCodeGlobals = std::move(newCode);
            installResult.codeBase = codeBase;
            installResult.newDataGlobals = std::move(newData);
            installResult.dataBase = dataBase;
            installResult.numDynVars = numDynVars;
            // GC root flags for the dynvars this install creates -- without
            // them VM::install defaults new slots to "not a root" and the GC
            // sweeps objects held only by a dynvar (e.g. synthdef.x's
            // `curGraphExprs during a graph trace).
            compiler.fillDynVarRootFlags(installResult);
            installResult.target = target;
            vm.install(installResult);
        }
    }

    // Run the init block of every loaded-but-uninitialized module. Module
    // init calls are only emitted (flag-guarded) into blocks that contain
    // the `import` declaration, so an eval that imports a module and then
    // fails its own typecheck or codegen installs the module's code and
    // globals but never runs its initializer -- leaving every module-level
    // `let` null for the rest of the session (a later eval that touches the
    // module then reads garbage and crashes). Initialize here, keyed on the
    // same runtime flag the emitted guards use; on the success path the
    // block's own guarded calls become no-ops. Call with the VM context
    // current (right after installPendingGlobals).
    void runPendingModuleInits() {
        for (auto* mod : typeChecker.allImportedModules()) {
            if (!mod || !mod->initBlock) continue;
            if (vm.global(mod->initFlagGlobalIndex).i != 0) continue;
            vm.callFunction(mod->initBlock, nullptr, 0);
            // A panicking init behaves like the emitted guard: the flag stays
            // clear so the next importing eval retries it.
            if (vm.isErrorHalted()) break;
            vm.global(mod->initFlagGlobalIndex).i = 1;
        }
    }
};

REPLSession::REPLSession(Compiler& compiler, VM& vm, const VMTarget& target,
                         std::vector<std::string> includePaths)
    : impl_(std::make_unique<Impl>(compiler, vm, target, std::move(includePaths)))
{}

REPLSession::REPLSession(Compiler& compiler, VM& vm, const VMTarget& target,
                         ModuleCompiler& moduleCompiler)
    : impl_(std::make_unique<Impl>(compiler, vm, target, moduleCompiler))
{}

REPLSession::~REPLSession() = default;

void REPLSession::setDisplayWidth(int width) {
    impl_->displayWidth = width < 1 ? 1 : width;
}

void REPLSession::setDocumentPath(const std::string& path) {
    impl_->documentPath = path;
    // Imports resolve relative to the importing file's directory first; give
    // the persistent TypeChecker the document's path so eval() input behaves
    // like text in that file. Empty clears the anchor (search paths only).
    impl_->typeChecker.setSourceFilePath(path);
}

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
        if (!impl_->documentPath.empty())
            codegen.setSourceFilePath(impl_->documentPath);
        codegen.enableRegReclaim = impl_->compiler.enableRegReclaim;
        codegen.enableConstFold = impl_->compiler.enableConstFold;
        codegen.enableTailCalls = impl_->compiler.enableTailCalls;
        block = lastIsExpr
            ? codegen.generateREPL(program)
            : codegen.generate(program);

        if (codegen.hasErrors()) {
            result.errors = codegen.errors();
            impl_->installPendingGlobals();
            impl_->runPendingModuleInits();
            return result;
        }

        // Sync reused globals: when a REPL function is redefined, it reuses
        // the old code-image index. genFnDecl stores the new CodeBlock in the
        // compiler's code globals, but installPendingGlobals only copies NEW
        // globals to the VM. Manually update the VM for any pre-existing code
        // slot (absolute index < codeBase) overwritten during codegen.
        u32 codeBase = kCodeGlobalBase + impl_->compiler.codeCompileBase();
        for (auto& item : program.items) {
            if (item->kind == ASTNode::FnDecl) {
                auto* fn = static_cast<FnDeclNode*>(item.get());
                u32 idx = (u32)fn->resolvedFuncGlobalIndex;
                if (fn->resolvedFuncGlobalIndex >= 0 && idx < codeBase) {
                    impl_->vm.global(idx) = impl_->compiler.global(idx);
                }
            }
        }
    }

    // Always harvest and install pending globals, and initialize any modules
    // this eval loaded -- even when the eval itself failed (see
    // runPendingModuleInits).
    impl_->installPendingGlobals();
    impl_->runPendingModuleInits();

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
            if (lastType && lastType->repr_ == Type::Repr::Inline) {
                // Inline composites arrive boxed in a single result Word;
                // the flat form is already correct.
                result.prettyValue = result.formattedValue;
            } else {
                VMString ps = prettyString(&value, lastType, impl_->displayWidth);
                result.prettyValue = toStdString(ps);
            }
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

    // Harvest any globals (and init any modules the query loaded)
    impl_->installPendingGlobals();
    impl_->runPendingModuleInits();

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
