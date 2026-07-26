-- audio_engine.x
-- Script wrapper for the audio engine FFI.
-- Re-exports the `audio_engine_ffi` module (registered by the bridge) so
-- users can write `import audio_engine.*;` and get both the FFI functions
-- and the enum definitions below.
-- Enums must be kept in sync with tzpl_client_interface.hpp and tzpl_plugin_abi.h.

export audio_engine_ffi.*;

enum Enable { kOff, kOn }

enum SchedPolicy {
    schedImmediate,
    schedBetterLateThanNever,
    schedOnTimeOnly,
}

enum FadeCurve {
    fadeLinear,
    fadeExponential,
    fadeSmoothstep,
    fadeEqualPower,
    fadeOutIn,
    fadeEaseInCubic,
    fadeEaseOutCubic,
}

enum Err {
    errNone,
    errInternal,
    errNodeIDAlreadyTaken,
    errNodeDefNotFound,
    errNodeNotFound,
    errNoteNotFound,
    errControlNotFound,
    errDeviceNotFound,
    errAlreadyAdded,
    errAlreadyRemoved,
    errSiloOutOfRange,
    errInputOutOfRange,
    errOutputOutOfRange,
    errNoAudioDevices,
    errAudioNotInitialized,
    errCommandsQueuedButNotSent,
    errNoActiveBundle,
    errEngineInUse,
    errCyclicConnection,
    errTypeMismatch,
    errRateMismatch,
    errChanMismatch,
    errNumPortsMismatch,
    errNotImplemented,
    errTooLate,
    errClockOutOfRange,
    errResourceLimit,
}

-- Signal tap modes (see tapOutlet / tapMaster).
enum TapMode {
    tapMeter,   -- peak/rms only
    tapScope,   -- peak/rms plus a sample FIFO, read with tapSamples
}

-- Spawn a coroutine task on slot `clock` of the CURRENT silo (call from silo
-- task code, e.g. inside start()). The coroutine yields beat-deltas (Float):
-- after each yield it resumes that many beats later; returning from the
-- coroutine (next -> None) stops the task. Returns a task id.
fn spawn(clock Int, c Coroutine<Float>) Int {
    scheduleTask(clock, fn() Float {
        let r = c next;
        if (r isSome) { r unwrap } else { 0.0 - 1.0 }
    })
}
