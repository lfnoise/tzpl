//
//  diagnostic.hpp
//  static-lang-3
//
//  Enhanced error diagnostics: source context, underlining, "did you mean?"
//

#ifndef diagnostic_hpp
#define diagnostic_hpp

#include "error.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <iostream>

namespace ts {

// Levenshtein edit distance between two strings
int editDistance(std::string_view a, std::string_view b);

// Find the closest match to `name` among `candidates`.
// Returns the best match if within maxDistance, or empty string if none.
std::string findClosestMatch(std::string_view name,
                             const std::vector<std::string>& candidates,
                             int maxDistance = 3);

// Format a single CompileError with source context and underlining.
// If useColor is true, ANSI escape codes are emitted.
std::string formatError(const CompileError& err,
                        const std::string& source,
                        const std::string& filename,
                        bool useColor);

// Print all errors to an output stream with source context.
void printDiagnostics(const std::vector<CompileError>& errors,
                      const std::string& source,
                      const std::string& filename,
                      std::ostream& out,
                      bool useColor);

// Format errors as plain strings (no ANSI colors), for the C API.
std::vector<std::string> formatErrorsPlain(
    const std::vector<CompileError>& errors,
    const std::string& source,
    const std::string& filename);

} // namespace ts

#endif /* diagnostic_hpp */
