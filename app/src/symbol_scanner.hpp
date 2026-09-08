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
//  symbol_scanner.hpp
//  app
//
//  A lexical scan of one Tzopilotl source text for the app's Find
//  Definitions: every place a name is introduced (fn, let/var/const,
//  struct/enum/type/constraint, enum cases, parameters, for-loop
//  variables, match bindings) with the text range it is visible in, plus
//  the file's import/export statements so the search can follow them into
//  modules. It tracks braces, comments and strings but does not parse
//  expressions -- it is a fast approximation of the compiler's scoping,
//  good enough to list overloads and shadowing declarations in the right
//  scope. No JUCE dependency (see text_search.hpp for the string choice).
//

#ifndef symbol_scanner_hpp
#define symbol_scanner_hpp

#include <string>
#include <vector>

namespace tzplapp {

enum class DefKind {
    function, variable, constant, structType, enumType, enumCase,
    typeAlias, constraint, parameter, loopVariable, binding,
};

// Short label for a definition kind ("fn", "let", "param", ...).
char const* defKindName(DefKind kind);

struct Definition {
    std::wstring name;
    DefKind kind = DefKind::function;
    int start = 0;       // offset of the name token
    int length = 0;
    int line = 0;        // 0-based
    int column = 0;      // 0-based
    // Where the definition is visible: [scopeStart, scopeEnd). A top-level
    // definition is visible everywhere in the file (and to importers).
    int scopeStart = 0;
    int scopeEnd = 0;
    bool topLevel = false;
    // `private` keyword or `_` prefix: not exported to importers.
    bool isPrivate = false;
};

struct ImportSpec {
    std::vector<std::wstring> path;   // a.b.c -> {a, b, c}
    bool wildcard = false;            // import a.b.*;
    bool isExport = false;            // export instead of import
    std::wstring alias;               // import a.b as alias;
    // import a.b.{x, y as z}; -> {x, ""}, {y, z}
    std::vector<std::pair<std::wstring, std::wstring>> names;
    int start = 0;                    // offset of the keyword
};

struct ScanResult {
    std::vector<Definition> defs;
    std::vector<ImportSpec> imports;
};

ScanResult scanSource(std::wstring const& text);

// True if `def` is visible at `pos` in the text it was scanned from.
bool definitionVisibleAt(Definition const& def, int pos);

// The identifier containing `pos`, or ending right before it (the caret
// sits after the last character when nothing is selected). Empty if none.
// `outStart` receives the identifier's offset.
std::wstring identifierAt(std::wstring const& text, int pos, int* outStart = nullptr);

bool isIdentStartChar(wchar_t c);
bool isIdentBodyChar(wchar_t c);

// The 0-based line/column of `offset` in `text`.
void lineColumnAt(std::wstring const& text, int offset, int& line, int& column);
// The text of the line containing `offset`, without its newline.
std::wstring lineTextAt(std::wstring const& text, int offset, int* outLineStart = nullptr);

}

#endif /* symbol_scanner_hpp */
