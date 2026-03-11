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
//  tzpl.hpp
//  Tzopilotl — C++ embedding API
//
//  Single include for embedding the Tzopilotl interpreter.
//
//  Usage:
//      #include <tzpl.hpp>
//
//      ts::TypeUniverse types;
//      ts::Compiler compiler(types);
//      ts::VMTarget target = compiler.createTarget();
//      ts::VM vm(64 * 1024 * 1024, types, target);
//
//      ts::CompileResult result = compiler.compile(source, "file.x", target);
//      vm.makeCurrent();
//      vm.install(result);
//      vm.execute(result.mainBlock);
//
//  Multiple VMs can share a target:
//      ts::VM vm2(64 * 1024 * 1024, types, target);
//      vm2.install(result);  // same result, same target — works
//
//  Key types:
//      ts::TypeUniverse  — shared type interning, one per process
//      ts::Compiler      — compiles source code, one per process (NRT thread)
//      ts::VMTarget      — shared compilation target (global layout)
//      ts::CompileResult — output of compilation, bridge between Compiler and VM
//      ts::VM            — executes compiled code, one per thread
//

#ifndef TZPL_HPP
#define TZPL_HPP

// Public embedding API headers
#include "vm.hpp"
#include "compiler.hpp"
#include "type_universe.hpp"
#include "error.hpp"

namespace ts {

// --- Exported function lookup ---

// Find an exported function by name (and optionally by param types for overload selection).
// Returns nullptr if not found.
inline const CompileResult::ExportedFunc* findExportedFunction(
    const CompileResult& result, const std::string& name,
    const std::vector<Type*>& paramTypes = {}) {
    for (auto& ef : result.exportedFunctions) {
        if (ef.name != name) continue;
        if (!paramTypes.empty()) {
            if (ef.paramTypes.size() != paramTypes.size()) continue;
            bool match = true;
            for (size_t i = 0; i < paramTypes.size(); ++i) {
                if (ef.paramTypes[i] != paramTypes[i]) { match = false; break; }
            }
            if (!match) continue;
        }
        return &ef;
    }
    return nullptr;
}

// --- Object accessor API ---

// String
const char* stringData(Obj* obj);
size_t      stringSize(Obj* obj);

// Fraction
int64_t fractionNumer(Obj* obj);
int64_t fractionDenom(Obj* obj);

// Complex
double complexReal(Obj* obj);
double complexImag(Obj* obj);

// Array
size_t  arraySize(Obj* obj);
int64_t arrayGetInt(Obj* obj, size_t index);
double  arrayGetFloat(Obj* obj, size_t index);
Obj*    arrayGetObj(Obj* obj, size_t index);

// Tuple
size_t tupleSize(Obj* obj);
Word   tupleGet(Obj* obj, size_t index);

// Struct
size_t structFieldCount(Obj* obj);
Word   structGetField(Obj* obj, size_t index);

} // namespace ts

#endif /* TZPL_HPP */
