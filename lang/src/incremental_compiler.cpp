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
//  incremental_compiler.cpp
//  lang
//

#include "incremental_compiler.hpp"
#include "compiler.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "type_checker.hpp"
#include "codegen.hpp"
#include "module_compiler.hpp"
#include "value.hpp"

namespace ts {

struct IncrementalCompiler::Impl {
    Compiler& compiler;
    VMTarget target;
    ModuleCompiler& moduleCompiler;
    TypeChecker typeChecker;
    // Keep every parsed Program alive: codegen records declNode pointers (e.g.
    // template instantiations) that later compiles may still dereference.
    std::vector<Program> programs;

    Impl(Compiler& c, const VMTarget& t, ModuleCompiler& mc)
        : compiler(c), target(t), moduleCompiler(mc),
          typeChecker(c, &moduleCompiler) {}
};

IncrementalCompiler::IncrementalCompiler(Compiler& compiler, const VMTarget& target,
                                         ModuleCompiler& moduleCompiler)
    : impl_(std::make_unique<Impl>(compiler, target, moduleCompiler)) {}

IncrementalCompiler::~IncrementalCompiler() = default;

CompileResult IncrementalCompiler::compile(const std::string& source,
                                           const std::string& filename) {
    CompileResult result;

    // Lex + parse. The Program is kept in impl_->programs (see Impl).
    Lexer lexer(source, filename);
    Parser parser(lexer);
    impl_->programs.push_back(parser.parseProgram());
    Program& program = impl_->programs.back();
    if (parser.hasErrors()) {
        result.errors = parser.errors();
        impl_->programs.pop_back();
        return result;
    }

    // Enter the compile context for this target. makeCurrent records each index
    // space's size, so takePending{Code,Data}Globals later slice only the slots
    // THIS call appends. Functions live in the CODE space: codeBase is the
    // absolute (ranged) index marking the redefinition cutoff -- a function with
    // a smaller index existed before this compile and was redefined in place.
    impl_->compiler.makeCurrent(impl_->target);
    u32 codeBase = kCodeGlobalBase + impl_->compiler.codeCompileBase();

    // Incremental type-check: reuses existing function/global indices, so a
    // redefined fn keeps its slot (the whole point of this class).
    impl_->typeChecker.setSourceFilePath(filename);
    impl_->typeChecker.setSourceText(source);
    impl_->typeChecker.checkREPLInput(program);
    if (impl_->typeChecker.hasErrors()) {
        result.errors = impl_->typeChecker.errors();
        impl_->compiler.endCurrent();
        return result;
    }

    // Codegen the top-level statements. Use generate (not generateREPL): a silo
    // module is a statement list installing defs, not a printable trailing expr.
    CodeGen codegen(impl_->compiler, impl_->typeChecker);
    codegen.setSourceFilePath(filename);
    codegen.setSourceText(source);
    codegen.enableRegReclaim = impl_->compiler.enableRegReclaim;
    codegen.enableConstFold = impl_->compiler.enableConstFold;
    codegen.enableTailCalls = impl_->compiler.enableTailCalls;
    CodeBlock* mainBlock = codegen.generate(program);
    if (codegen.hasErrors()) {
        result.errors = codegen.errors();
        impl_->compiler.endCurrent();
        return result;
    }

    // Capture redefined functions: genFnDecl wrote the new CodeBlock into the
    // function's EXISTING code slot (absolute index < codeBase). takePendingCode
    // only returns slots appended this compile, so these in-place overwrites
    // would be lost. Collect them as reusedGlobals for VM::install to apply by
    // index. (Mirrors the REPL's reused-global sync, packaged for cross-thread
    // install.)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::FnDecl) {
            auto* fn = static_cast<FnDeclNode*>(item.get());
            if (fn->resolvedFuncGlobalIndex >= 0 &&
                (u32)fn->resolvedFuncGlobalIndex < codeBase) {
                u32 idx = (u32)fn->resolvedFuncGlobalIndex;
                result.reusedGlobals.push_back({idx, impl_->compiler.global(idx)});
            }
        }
    }

    // Harvest appended globals + dynvar/exported-function metadata.
    impl_->compiler.finalizeResult(result, impl_->typeChecker, mainBlock, impl_->target);

    impl_->compiler.endCurrent();
    return result;
}

} // namespace ts
