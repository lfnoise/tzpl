//
//  langx_synthdef_compiler_ffi.hpp
//  bridge
//
//  FFI bridge: registers synthdef-compiler functions as callable
//  foreign functions in the Language X compiler/VM.
//

#ifndef langx_synthdef_compiler_ffi_hpp
#define langx_synthdef_compiler_ffi_hpp

namespace ts { class Compiler; }

namespace bridge {

// Register all synthdef-compiler FFI functions with the Language X compiler.
// Must be called BEFORE compiling any Language X source that references these functions.
// The engine pointer is obtained from vm.userData() (set via setEngineOnVM).
void registerSynthdefCompilerFFI(ts::Compiler& compiler);

} // namespace bridge

#endif /* langx_synthdef_compiler_ffi_hpp */
