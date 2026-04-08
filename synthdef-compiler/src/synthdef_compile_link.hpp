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

// Get the base build directory for compiled plugins.
// Uses $TZPL_BUILD if set, else ~/tzpl-build/.
string getBuildDir();

// Ensure the build directory subdirectories (include/, cpp/, dylib/) exist
// and copy shared headers into include/. Call once before compile workflows.
void ensureBuildDirs(string const& buildDir);

// Write generated C++ code to {buildDir}/cpp/{synthName}_synth.cpp.
void writeCodeToFile(string const& buildDir, string const& synthName, string const& ccode);

// Compile and link a synthdef plugin to a .dylib.
// Files: cpp/{name}.cpp -> cpp/{name}.o -> dylib/{name}.dylib
// Returns 0 on success, non-zero on failure.
int compileAndLink(string const& buildDir, string const& synthName);

// Return the path to a compiled dylib given base dir and synth name.
string dylibPath(string const& buildDir, string const& synthName);

// Result of loading a compiled .dylib plugin.
struct LoadedDef {
    tzpl_SynthDef def;
    void* dlHandle;
};

// Load a compiled .dylib and return the tzpl_SynthDef and dlopen handle.
optional<LoadedDef> loadDef(std::string path);

} // namespace synthdef
