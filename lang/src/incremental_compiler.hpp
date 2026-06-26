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
//  incremental_compiler.hpp
//  lang
//
//  Persistent, incremental compile context for one VM target.
//

#ifndef incremental_compiler_hpp
#define incremental_compiler_hpp

#include <memory>
#include <string>

namespace ts {

class Compiler;
class ModuleCompiler;
struct VMTargetData;
using VMTarget = std::shared_ptr<VMTargetData>;
struct CompileResult;

// Persistent, incremental compiler for a single VM target (e.g. one silo VM).
//
// Compiler::compile uses a FRESH TypeChecker per call, so every call re-registers
// builtins and re-allocates all globals -- a function "redefined" by a later call
// lands in a brand-new global slot the earlier code never sees. This class keeps
// ONE TypeChecker across calls (via checkREPLInput, the same incremental path the
// REPL uses), so a redefined function reuses its existing global index and live
// call sites pick up the new body.
//
// It is compile-only: it never executes the produced main block. The CompileResult
// it returns can be shipped to a VM living on another thread (the silo RT thread).
// A redefined function reuses its code slot in the shared target; the silo install
// path snapshots the whole code layout into a fresh image, so redefinitions are
// captured without any per-result delta.
class IncrementalCompiler {
public:
    // compiler and moduleCompiler are borrowed (owned by the caller). target is
    // the shared VM target whose global layout this session grows.
    IncrementalCompiler(Compiler& compiler, const VMTarget& target,
                        ModuleCompiler& moduleCompiler);
    ~IncrementalCompiler();

    IncrementalCompiler(const IncrementalCompiler&) = delete;
    IncrementalCompiler& operator=(const IncrementalCompiler&) = delete;

    // Compile a source fragment against the persistent type-check context.
    // Returns a CompileResult ready for VM::install on any thread (on success).
    // On failure, result.success == false and result.errors is populated; the
    // thread-local compile context is always cleared before returning.
    CompileResult compile(const std::string& source, const std::string& filename);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ts

#endif /* incremental_compiler_hpp */
