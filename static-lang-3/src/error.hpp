//
//  error.hpp
//  static-lang-3
//
//  Compile error types for lexer, parser, and type checker
//

#ifndef error_hpp
#define error_hpp

#include "base_types.hpp"
#include <string>
#include <vector>
#include <stdexcept>

namespace ts {

// Source location
struct SourceLoc {
    u32 line = 1;
    u32 col = 1;
    u32 offset = 0;
};

// Range in source code
struct SourceRange {
    SourceLoc start;
    SourceLoc end;

    SourceRange() = default;
    SourceRange(SourceLoc s, SourceLoc e) : start(s), end(e) {}
};

// Compile error with source location and message
struct CompileError {
    enum Kind {
        LexError,
        ParseError,
        TypeError,
    };

    Kind kind;
    SourceRange loc;
    std::string message;

    CompileError(Kind k, SourceRange l, std::string msg)
        : kind(k), loc(l), message(std::move(msg)) {}
};

// Exception thrown when compilation fails
class CompileException : public std::runtime_error {
public:
    std::vector<CompileError> errors;

    CompileException(std::vector<CompileError> errs)
        : std::runtime_error(errs.empty() ? "compilation failed" : errs[0].message)
        , errors(std::move(errs)) {}

    CompileException(CompileError err)
        : std::runtime_error(err.message)
        , errors({std::move(err)}) {}
};

} // namespace ts

#endif /* error_hpp */
