//
//  tzpl_synthdef_compiler_ffi.hpp
//  bridge
//
//  FFI bridge: registers synthdef-compiler functions as callable
//  foreign functions in the Tzopilotl compiler/VM.
//

#ifndef tzpl_synthdef_compiler_ffi_hpp
#define tzpl_synthdef_compiler_ffi_hpp

namespace ts { class Compiler; }

namespace bridge {

// Register all synthdef-compiler FFI functions with the Tzopilotl compiler.
// Must be called BEFORE compiling any Tzopilotl source that references these functions.
// The engine pointer is obtained from vm.userData() (set via setEngineOnVM).
void registerSynthdefCompilerFFI(ts::Compiler& compiler);

} // namespace bridge

#endif /* tzpl_synthdef_compiler_ffi_hpp */
