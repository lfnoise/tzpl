//
//  langx_audio_engine_ffi.hpp
//  bridge
//
//  FFI bridge: registers audio-engine client functions as callable
//  foreign functions in the Language X compiler/VM.
//

#ifndef langx_audio_engine_ffi_hpp
#define langx_audio_engine_ffi_hpp

namespace ts { class Compiler; }
namespace engine { struct Engine; }

namespace bridge {

// Register all audio-engine FFI functions with the Language X compiler.
// Must be called BEFORE compiling any Language X source that references these functions.
void registerAudioEngineFFI(ts::Compiler& compiler);

// Attach the engine pointer to a VM so FFI functions can access it.
// Must be called BEFORE executing any code that calls engine FFI functions.
// Uses the VM's userData pointer.
void setEngineOnVM(void* vm, engine::Engine* engine);

} // namespace bridge

#endif /* langx_audio_engine_ffi_hpp */
