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
//  diagnostic.hpp
//  lang
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
