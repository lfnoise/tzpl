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
//  vm_config.hpp
//  lang
//
//  Per-VM construction-time configuration. Kept in its own lightweight
//  header so host code (the bridge's AppContext, the app's settings) can
//  hold a VMConfig without pulling in all of vm.hpp.
//

#ifndef vm_config_hpp
#define vm_config_hpp

#include "base_types.hpp"

namespace ts {

// Every field carries the historical default, so `VMConfig{}` (or the legacy
// poolSize-only VM constructor) reproduces the pre-config behavior exactly.
// Hosts populate this from user settings (the app's advanced language
// settings) before constructing a VM.
//
// The execution-structure limits are hard caps: overflow raises a runtime
// error rather than growing. The behavioral limits seed per-VM values that
// running code can adjust later via builtins (setGraphMaxDepth etc.).
struct VMConfig {
    // --- memory ---
    usize poolSize        = 64 * 1024 * 1024;           // initial TLSF pool bytes
    usize growthChunkMin  = 64ULL * 1024 * 1024;        // heap-growth chunk floor
    usize growthChunkMax  = 4ULL * 1024 * 1024 * 1024;  // heap-growth chunk ceiling

    // --- execution structures ---
    u32 maxRegs            = 4096;  // register file (Words)
    u32 maxFrames          = 512;   // call-frame stack depth
    u32 maxDynStack        = 256;   // dynamic-scope save entries
    u32 maxDynStackPayload = 1024;  // inline-composite dynvar save buffer (Words)

    // --- GC ---
    u64 gcStepBudgetNanos  = 2'000'000;  // == VM::kSafepointStepNanos
    u32 gcMinTriggerAllocs = 4096;       // == TracingGC::kMinTriggerAllocs
    u32 gcGrowthFactor     = 4;          // == TracingGC::kGrowthFactor
    bool mmuEnabled        = false;      // RT hosts enable at attach
    u32 mmuMutatorPermille = 900;        // 90% mutator
    u64 mmuWindowNanos     = 15'000'000; // 15 ms window

    // --- behavioral limits (live-adjustable via builtins afterwards) ---
    u32 graphMaxDepth  = 10000;  // == kGraphMaxDepth (value_graph.hpp)
    i64 lazyForceLimit = 10000;  // == kLazyForceLimit
    u32 printMaxDepth  = 200;    // == kPrintMaxDepth
    i64 listPrintLimit = 10;
};

} // namespace ts

#endif /* vm_config_hpp */
