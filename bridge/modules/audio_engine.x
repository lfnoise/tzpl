-- audio_engine.x
-- Enum definitions mirroring the C++ audio engine enums.
-- These must be kept in sync with jscs_client_interface.hpp and jscs_plugin_abi.h.

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
}
