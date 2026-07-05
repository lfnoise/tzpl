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
//  repl_session.hpp
//  lang
//
//  Incremental REPL compilation session with pimpl idiom.
//  Only depends on public API types (Compiler, VM, VMTarget, CompileError).
//

#ifndef repl_session_hpp
#define repl_session_hpp

#include "compiler.hpp"
#include "error.hpp"
#include <string>
#include <vector>
#include <memory>

namespace ts {

class ModuleCompiler;

class REPLSession {
public:
    REPLSession(Compiler& compiler, VM& vm, const VMTarget& target,
                std::vector<std::string> includePaths = {});
    // Use an existing ModuleCompiler (e.g. from a prior runSource) so that
    // cached modules and their type objects are reused, avoiding dynamic
    // variable type conflicts on re-import.
    REPLSession(Compiler& compiler, VM& vm, const VMTarget& target,
                ModuleCompiler& moduleCompiler);
    ~REPLSession();

    // Result of evaluating a REPL input
    struct EvalResult {
        bool success = false;
        bool hasValue = false;
        std::string formattedValue;  // "42", "'hello", "[1, 2, 3]" (flat)
        std::string prettyValue;     // width-aware, possibly multi-line
        std::string typeName;        // "Int", "Symbol", "[Int]"
        std::string source;          // echoed back for error formatting
        std::vector<CompileError> errors;
    };

    // Target line width for EvalResult::prettyValue (default 80).
    void setDisplayWidth(int width);

    // Evaluate a line of input (lex -> parse -> typecheck -> codegen -> execute)
    EvalResult eval(const std::string& input);

    // Type-check an expression and return its type name
    EvalResult queryType(const std::string& expr);

    // List all global variables as formatted strings ("let x : Int", "var y : Float")
    std::vector<std::string> listGlobals() const;

    // List all user-defined functions as formatted strings ("fn foo(Int, Int) Int")
    std::vector<std::string> listFunctions() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ts

#endif /* repl_session_hpp */
