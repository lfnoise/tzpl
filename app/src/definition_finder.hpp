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
//  definition_finder.hpp
//  app
//
//  Find Definitions: given a caret in a document, the identifier there
//  (or the `module.name` it qualifies), every definition of that name in
//  scope -- enclosing blocks, the file's top level, then the modules the
//  file imports (following `export` re-exports), resolved the way the
//  compiler resolves them: relative to the importing file, then the user
//  include paths, then the stdlib. Built on symbol_scanner.hpp; no JUCE.
//

#ifndef definition_finder_hpp
#define definition_finder_hpp

#include "symbol_scanner.hpp"
#include <functional>
#include <string>
#include <vector>

namespace tzplapp {

struct DefinitionHit {
    std::string path;          // empty: the query document itself
    std::wstring modulePath;   // dotted module name, empty for the document
    Definition def;
    std::wstring lineText;     // the source line holding the definition
};

struct DefinitionQuery {
    std::wstring text;         // the document being edited
    std::string path;          // its file path ("" if untitled)
    int caret = 0;             // offset of the identifier to look up
    // Module search directories after the importing file's own directory,
    // in order (user include paths, then the stdlib).
    std::vector<std::string> searchDirs;
    // Text provider, so open-but-unsaved editor tabs are searched as they
    // are on screen. Default: read the file from disk.
    std::function<bool(std::string const& path, std::wstring& out)> readFile;
};

struct DefinitionResult {
    std::wstring name;         // the identifier looked up (empty: none at caret)
    std::wstring qualifier;    // `q` in `q.name`, if the caret is on one
    std::vector<DefinitionHit> hits;
    // Imports that could not be resolved to a file (dotted names).
    std::vector<std::wstring> unresolvedModules;
};

DefinitionResult findDefinitions(DefinitionQuery const& query);

// UTF-8 <-> wide (UTF-32 on macOS/Linux) helpers shared by the app.
std::wstring utf8ToWide(std::string const& s);
std::string wideToUtf8(std::wstring const& w);
bool readTextFileWide(std::string const& path, std::wstring& out);

}

#endif /* definition_finder_hpp */
