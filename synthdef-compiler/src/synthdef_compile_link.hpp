//
//  synthdef_compile_link.hpp
//  synthdef-compiler
//
//  Compilation and linking functions for synthdef plugins.
//  These are part of the library so that the FFI bridge can use them.
//

#pragma once

#include "synthdef_types2.hpp"
#include "jscs_plugin_abi.h"

namespace synthdef {

// Get the build directory for compiled plugins.
// Uses $SAPF3_BUILD if set, else ~/sapf-build-5/, else /tmp/.
string getBuildDir();

// Write generated C++ code to a file in the given directory.
void writeCodeToFile(string dir, string synthName, string ccode);

// Compile and link a synthdef plugin to a .dylib.
// Returns 0 on success, non-zero on failure.
int compileAndLink(string dir, string synthName);

// Load a compiled .dylib and return the jscs_SynthDef it exports.
optional<jscs_SynthDef> loadDef(std::string path);

} // namespace synthdef
