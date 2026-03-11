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
//  synthdef_compile_link.hpp
//  synthdef-compiler
//
//  Compilation and linking functions for synthdef plugins.
//  These are part of the library so that the FFI bridge can use them.
//

#pragma once

#include "synthdef_types2.hpp"
#include "tzpl_plugin_abi.h"

namespace synthdef {

// Get the build directory for compiled plugins.
// Uses $TZPL_BUILD if set, else ~/tzpl-build/, else /tmp/.
string getBuildDir();

// Write generated C++ code to a file in the given directory.
void writeCodeToFile(string dir, string synthName, string ccode);

// Compile and link a synthdef plugin to a .dylib.
// Returns 0 on success, non-zero on failure.
int compileAndLink(string dir, string synthName);

// Load a compiled .dylib and return the tzpl_SynthDef it exports.
optional<tzpl_SynthDef> loadDef(std::string path);

} // namespace synthdef
