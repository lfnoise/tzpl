//
//  repl_session.hpp
//  static-lang-3
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

class REPLSession {
public:
    REPLSession(Compiler& compiler, VM& vm, const VMTarget& target,
                std::vector<std::string> includePaths = {});
    ~REPLSession();

    // Result of evaluating a REPL input
    struct EvalResult {
        bool success = false;
        bool hasValue = false;
        std::string formattedValue;  // "42", "'hello", "[1, 2, 3]"
        std::string typeName;        // "Int", "Symbol", "[Int]"
        std::string source;          // echoed back for error formatting
        std::vector<CompileError> errors;
    };

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
