//
//  tzpl_audio_engine_ffi.hpp
//  bridge
//
//  FFI bridge: registers engine client functions as callable
//  foreign functions in the Tzopilotl compiler/VM.
//

#ifndef tzpl_audio_engine_ffi_hpp
#define tzpl_audio_engine_ffi_hpp

namespace ts { class Compiler; }
namespace engine { struct Engine; }

namespace bridge {

// Register all engine FFI functions with the Tzopilotl compiler.
// Must be called BEFORE compiling any Tzopilotl source that references these functions.
void registerAudioEngineFFI(ts::Compiler& compiler);

// Attach the engine pointer to a VM so FFI functions can access it.
// Must be called BEFORE executing any code that calls engine FFI functions.
// Uses the VM's userData pointer.
void setEngineOnVM(void* vm, engine::Engine* engine);

} // namespace bridge

#endif /* tzpl_audio_engine_ffi_hpp */
