-- audio_engine.x
-- Script wrapper for the audio engine FFI.
-- Re-exports the `audio_engine_ffi` module (registered by the bridge) so
-- users can write `import audio_engine.*;` and get both the FFI functions
-- and the enum definitions below.
-- Enums must be kept in sync with tzpl_client_interface.hpp and tzpl_plugin_abi.h.

export audio_engine_ffi.*;

enum Enable { kOff, kOn }

-- How a silo runs a cycle of connections between nodes. feedbackSample (the
-- default): the cycle's nodes run a sample at a time, the connection that
-- closes it a sample late, as on the per-sample engine. feedbackBlock: whole
-- blocks, that connection a block late (cheaper).
enum FeedbackMode { feedbackSample, feedbackBlock }

fn feedbackMode(silo Int, mode FeedbackMode) Void { setFeedbackMode(silo, mode ordinal); }

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

-- Input values (block engine: synths have no controls; every parameter is
-- an input, and an unconnected input holds a value). Address the input by
-- port index or by name; give one value or an array, which broadcasts
-- cyclically over the addressed channels. All are bundled commands
-- (between begin() and go()/sched()), applied at the bundle's sample.
--
--   setInput(node, port, value)              step the whole input
--   setInput(node, port, value, rampTime)    ramp from the current value
--   setInputAt(node, port, first, values)    flat channels [first, first+n)
--   setInputBox(node, port, offset, extent, values)
--                                            a box in shape coordinates
--                                            (extent 0 = to the end)
--   setInputX(node, port, value, xfadeTime)  crossfade to the value; a
--                                            connected input fades its
--                                            signal out and is left holding it
--   impulseInput(node, port, value)          the value for one sample (a
--   impulseInputAt(node, port, first, values) trigger), then back
--
-- On a connected input setInput/setInputAt change the value it reverts to
-- when disconnected.

fn _portIndex(port Int) Int = port;
fn _portIndex(port String) Int = -1;
fn _portName(port Int) String = "";
fn _portName(port String) String = port;
fn _inputValues(v Float) [Float] = [v];
fn _inputValues(v Int) [Float] = [v toFloat];
fn _inputValues(vs [Float]) [Float] = vs;

fn setInput<P, V>(nodeID Int, port P, value V, rampTime Float = 0.0,
                  curve FadeCurve = FadeCurve.fadeLinear) Int =
	inputOp(nodeID, _portIndex(port), _portName(port), 0, 0, _inputValues(value),
	        rampTime, curve ordinal, 0);

fn setInputAt<P, V>(nodeID Int, port P, first Int, value V, rampTime Float = 0.0,
                    curve FadeCurve = FadeCurve.fadeLinear) Int {
	let vs = _inputValues(value);
	inputOp(nodeID, _portIndex(port), _portName(port), first, vs length, vs,
	        rampTime, curve ordinal, 0)
}

fn setInputBox<P, V>(nodeID Int, port P, offset [Int], extent [Int], value V,
                     rampTime Float = 0.0, curve FadeCurve = FadeCurve.fadeLinear) Int =
	inputOpBox(nodeID, _portIndex(port), _portName(port), offset, extent,
	           _inputValues(value), rampTime, curve ordinal, 0);

fn setInputX<P, V>(nodeID Int, port P, value V, xfadeTime Float,
                   curve FadeCurve = FadeCurve.fadeLinear) Int =
	inputOp(nodeID, _portIndex(port), _portName(port), 0, 0, _inputValues(value),
	        xfadeTime, curve ordinal, 1);

fn impulseInput<P, V>(nodeID Int, port P, value V) Int =
	inputOp(nodeID, _portIndex(port), _portName(port), 0, 0, _inputValues(value),
	        0.0, 0, 2);

fn impulseInputAt<P, V>(nodeID Int, port P, first Int, value V) Int {
	let vs = _inputValues(value);
	inputOp(nodeID, _portIndex(port), _portName(port), first, vs length, vs, 0.0, 0, 2)
}

-- One zone of a sample bank: a sound file covering an inclusive pitch and
-- velocity range (MIDI 0-127). rootKey is the note at which the sample
-- plays back unshifted; loopStart/loopEnd are a sustain loop in frames of
-- the source file (loopEnd exclusive, fractional values allowed). Negative
-- rootKey / loopStart / loopEnd mean "take it from the file's instrument
-- metadata" (the WAV smpl or AIFF INST chunk); where the file has none
-- either, rootKey defaults to loKey and the zone has no loop. Each zone's
-- (pitch, velocity) rectangle is a tile: no two tiles may cover the same
-- cell, but two zones may share a pitch range if their velocity ranges are
-- disjoint (velocity layering), and vice versa.
struct SampleZone {
    path String;
    loKey Int;
    hiKey Int;
    loVel Int;
    hiVel Int;
    rootKey Int;
    loopStart Float;
    loopEnd Float;
}

fn sampleZone(path String, loKey Int, hiKey Int = -1, loVel Int = 0,
              hiVel Int = 127, rootKey Int = -1,
              loopStart Float = -1.0, loopEnd Float = -1.0) SampleZone {
    SampleZone { path: path, loKey: loKey, hiKey: hiKey < 0 ? loKey : hiKey,
                 loVel: loVel, hiVel: hiVel, rootKey: rootKey,
                 loopStart: loopStart, loopEnd: loopEnd }
}

-- Loads a sample bank into a node's bank slot (bundled command, like
-- loadBuffer). Files load and zones validate at submit on the calling
-- thread; a bad spec (including an explicit loop with loopEnd <= loopStart
-- or loopEnd past the end of the file) or missing file returns
-- errBadSampleBank.
fn loadSampleBank(nodeID Int, bankID Int, zones [SampleZone]) Int {
    loadSampleBank(nodeID, bankID,
        zones map(fn(z SampleZone) String { z.path }),
        zones map(fn(z SampleZone) Int { z.loKey }),
        zones map(fn(z SampleZone) Int { z.hiKey }),
        zones map(fn(z SampleZone) Int { z.loVel }),
        zones map(fn(z SampleZone) Int { z.hiVel }),
        zones map(fn(z SampleZone) Int { z.rootKey }),
        zones map(fn(z SampleZone) Float { z.loopStart }),
        zones map(fn(z SampleZone) Float { z.loopEnd }))
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
