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
    errBadSampleBank,
}

-- One zone of a sample bank: a sound file covering an inclusive pitch and
-- velocity range (MIDI 0-127). rootKey is the note at which the sample
-- plays back unshifted; -1 means "use loKey". Each zone's (pitch, velocity)
-- rectangle is a tile: no two tiles may cover the same cell, but two zones
-- may share a pitch range if their velocity ranges are disjoint (velocity
-- layering), and vice versa.
struct SampleZone {
    path String;
    loKey Int;
    hiKey Int;
    loVel Int;
    hiVel Int;
    rootKey Int;
}

fn sampleZone(path String, loKey Int, hiKey Int = -1, loVel Int = 0,
              hiVel Int = 127, rootKey Int = -1) SampleZone {
    SampleZone { path: path, loKey: loKey, hiKey: hiKey < 0 ? loKey : hiKey,
                 loVel: loVel, hiVel: hiVel, rootKey: rootKey }
}

-- Loads a sample bank into a node's bank slot (bundled command, like
-- loadBuffer). Files load and zones validate at submit on the calling
-- thread; a bad spec or missing file returns errBadSampleBank.
fn loadSampleBank(nodeID Int, bankID Int, zones [SampleZone]) Int {
    loadSampleBank(nodeID, bankID,
        zones map(fn(z SampleZone) String { z.path }),
        zones map(fn(z SampleZone) Int { z.loKey }),
        zones map(fn(z SampleZone) Int { z.hiKey }),
        zones map(fn(z SampleZone) Int { z.loVel }),
        zones map(fn(z SampleZone) Int { z.hiVel }),
        zones map(fn(z SampleZone) Int { z.rootKey }))
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
