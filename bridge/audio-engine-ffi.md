# Audio Engine FFI Reference

Tzopilotl bindings for the audio engine, registered by `bridge::registerAudioEngineFFI()`.

All functions that modify the audio graph must be called between `begin()` and `go()` (or `sched()`). Error-returning functions return `Int` where `0` = success.

Enum constants (`FadeCurve`, `SchedPolicy`, `Err`, `Enable`) are defined in the Tzopilotl module `audio_engine.x` (in `bridge/modules/`). Import with `import audio_engine.*;`. Simple enums are implicitly coerced to `Int` when passed to FFI functions.

## Engine Lifecycle

### `engineStart() -> Void`
Start the audio stream. Must be called before any audio is produced.

### `engineStop() -> Void`
Stop the audio stream.

### `isAudioRunning() -> Bool`
Returns `true` if the audio stream is currently running.

### `getStreamTime() -> Float`
Returns the current audio stream time in seconds. Useful for scheduling events relative to "now".

### `masterGain(gain: Float) -> Void`
Set the master output gain. `1.0` = unity, `0.0` = silence. When the safety limiter is enabled, the master gain can reduce the limiter's gain but never increase it -- it will not fight the limiter. When audio is below the limit, the master gain applies freely. When the safety limiter is disabled, the master gain is a simple multiply.

### `safetyLimiter(on: Bool) -> Void`
Enable or disable the safety limiter on the master output. When enabled, output is hard-limited to prevent clipping/damage.

### `inputChannels() -> Int`
Returns the number of active hardware input channels (`0` if audio input is disabled).

### `sleep(seconds: Float) -> Void`
Block the calling thread for the given duration. **NRT only.** This is a temporary function that will be replaced by a proper event scheduler.

## Plugin Loading

### `loadPlugins(path: String) -> Bool`
Load all plugin definitions (`.so`/`.dylib`) found in the directory at `path`. Returns `true` on success.

### `loadPlugin(path: String, name: String) -> Bool`
Load a single plugin definition from a shared library at `path` with the given `name`. Returns `true` on success.

## Command Bundling

Commands that modify the audio graph are not executed immediately. They are collected into a bundle and submitted atomically.

### `begin(silo: Int) -> Int`
Begin a new command bundle targeting the given silo (audio worker thread). Silo indices start at `0`. Returns an error code.

### `go() -> Int`
Submit the current command bundle for immediate execution on the next audio callback. Returns an error code.

### `sched(time: Float) -> Int`
Submit the current command bundle for execution at the given stream time (in seconds). Uses the default scheduling policy. Returns an error code.

### `schedPolicy(time: Float, policy: Int) -> Int`
Submit the current command bundle for execution at the given stream time with an explicit scheduling policy. Returns an error code.

**Scheduling policies** (from `SchedPolicy` enum):

| Value | Description |
|---|---|
| `SchedPolicy.schedImmediate` | Execute immediately, ignoring the timestamp |
| `SchedPolicy.schedBetterLateThanNever` | Execute even if the scheduled time has passed |
| `SchedPolicy.schedOnTimeOnly` | Drop the bundle if the scheduled time has passed |

## Node Operations

These must be called within a `begin()`/`go()` block.

### `newNode(defName: String, nodeID: Int) -> Int`
Create a new node instance from the plugin definition named `defName`, assigning it the given `nodeID`. The node ID must be unique within the silo. Returns an error code.

### `freeNode(nodeID: Int) -> Int`
Remove and free the node with the given ID. Returns an error code.

### `freeAllNodes() -> Int`
Remove and free all nodes on the current silo. Returns an error code.

### `channelOffset(offset: Int) -> Int`
Set the channel offset for the current silo's output in the hardware buffer. With an offset of `2` on a 4-channel output, the silo writes to channels 2-3 instead of 0-1. This allows different silos to target different hardware output channels (e.g., for surround sound or multi-speaker setups). The offset is clamped to the hardware channel count. Default is `0`. Returns an error code.

## Connections

These must be called within a `begin()`/`go()` block. Node `0` is the hardware output; connecting to `(0, 0)` sends audio to the output. Node `1` is the hardware input; connecting from `(1, 0)` receives live audio (requires audio input to be enabled).

### `connect(srcNode: Int, srcPort: Int, dstNode: Int, dstPort: Int) -> Int`
Connect an output port to an input port. The connection takes effect instantly (no crossfade).

### `connectX(srcNode: Int, srcPort: Int, dstNode: Int, dstPort: Int, xfade: Float, curve: Int) -> Int`
Connect with a crossfade. `xfade` is the fade duration in seconds. `curve` is a `FadeCurve` ordinal.

### `disconnectInput(dstNode: Int, dstPort: Int) -> Int`
Disconnect whatever is connected to the given input port. Takes effect instantly.

### `disconnectInputX(dstNode: Int, dstPort: Int, xfade: Float, curve: Int) -> Int`
Disconnect an input port with a crossfade to silence.

### `disconnectOutput(srcNode: Int, srcPort: Int) -> Int`
Disconnect all connections from the given output port.

### `disconnectNode(nodeID: Int) -> Int`
Disconnect all inputs and outputs of the given node.

### `reconnectOutput(oldSrcNode: Int, oldSrcPort: Int, newSrcNode: Int, newSrcPort: Int, xfade: Float, curve: Int) -> Int`
Move all connections from one output port to another, crossfading over `xfade` seconds.

### `replaceNode(oldNodeID: Int, newNodeID: Int, xfade: Float, curve: Int) -> Int`
Replace one node with another, reconnecting all inputs and outputs and crossfading over `xfade` seconds. The old node is freed after the fade completes.

## Parameter Control

These must be called within a `begin()`/`go()` block.

### `setInput(nodeID: Int, portIndex: Int, value: Float) -> Int`
Set an input port to a constant float value (no crossfade). Overrides any existing connection on that port.

### `setInputX(nodeID: Int, portIndex: Int, value: Float, xfade: Float, curve: Int) -> Int`
Set an input port to a constant float value with a crossfade from the current value.

### `setControl(nodeID: Int, controlID: Int, value: Float) -> Int`
Set a control parameter on a node. Controls are node-specific named parameters distinct from audio-rate input ports.

## Note / Voice Management

For nodes that support polyphonic voice allocation (e.g., voicer nodes). These must be called within a `begin()`/`go()` block.

### `noteOn(nodeID: Int, noteID: Int, params: Array[Float]) -> Int`
Start a new voice on the given voicer node. `noteID` identifies the voice for later `noteOff`/`noteSetParams` calls. `params` is an array of initial parameter values passed to the voice.

### `noteOff(nodeID: Int, noteID: Int) -> Int`
Release the voice identified by `noteID` on the given voicer node.

### `allNotesOff(nodeID: Int) -> Int`
Release all active voices on the given voicer node.

### `noteSetParams(nodeID: Int, noteID: Int, firstParam: Int, values: Array[Float]) -> Int`
Update parameters on an active voice. Sets `len(values)` consecutive parameters starting at index `firstParam`.

## Introspection

### `listSynthDefs() -> Array[String]`
Returns an array of the names of all registered node definitions (synth plugins).

## Enums (from `audio_engine.x` module)

Import with `import audio_engine.*;` at the top of your script.

### `FadeCurve`
Pass directly to any function that accepts a `curve: Int` parameter.

| Case | Description |
|---|---|
| `fadeLinear` | Linear interpolation |
| `fadeExponential` | Exponential curve (perceptually even volume change) |
| `fadeSmoothstep` | Hermite smoothstep (S-curve) |
| `fadeEqualPower` | Equal-power crossfade (constant energy) |
| `fadeOutIn` | Fade out then fade in (dip in the middle) |
| `fadeEaseInCubic` | Cubic ease-in (slow start, fast end) |
| `fadeEaseOutCubic` | Cubic ease-out (fast start, slow end) |

### `SchedPolicy`
Pass directly to `schedPolicy()`.

| Case | Description |
|---|---|
| `schedImmediate` | Execute immediately, ignoring the timestamp |
| `schedBetterLateThanNever` | Execute even if the scheduled time has passed |
| `schedOnTimeOnly` | Drop the bundle if the scheduled time has passed |

### `Err`
Error codes returned by FFI functions. Compare via `ordinal(Err.errNone)` or assign to `Int`.

| Case | Description |
|---|---|
| `errNone` | Success (0) |
| `errInternal` | Internal engine error |
| `errNodeIDAlreadyTaken` | Node ID already in use on this silo |
| `errNodeDefNotFound` | No plugin definition with the given name |
| `errNodeNotFound` | No node with the given ID |
| `errNoteNotFound` | No active note with the given ID |
| `errControlNotFound` | No control with the given ID |
| `errDeviceNotFound` | Audio device not found |
| `errAlreadyAdded` | Node was already added to the silo |
| `errAlreadyRemoved` | Node was already removed from the silo |
| `errSiloOutOfRange` | Silo index out of range |
| `errInputOutOfRange` | Input port index out of range |
| `errOutputOutOfRange` | Output port index out of range |
| `errNoAudioDevices` | No audio devices available |
| `errAudioNotInitialized` | Audio stream not initialized |
| `errCommandsQueuedButNotSent` | Commands queued but `go()`/`sched()` not called before next `begin()` |
| `errNoActiveBundle` | No `begin()` was called before commands |
| `errEngineInUse` | Engine is already in use |
| `errCyclicConnection` | Connection would create a cycle |
| `errTypeMismatch` | Port element types do not match |
| `errRateMismatch` | Port signal rates do not match |
| `errChanMismatch` | Port channel counts do not match |
| `errNumPortsMismatch` | Number of ports does not match |
| `errNotImplemented` | Feature not yet implemented |
| `errTooLate` | Scheduled event missed its deadline |

### `Enable`
| Case | Description |
|---|---|
| `kOff` | Disabled (0) |
| `kOn` | Enabled (1) |

## Example

```
import audio_engine.*;

-- Create a 440 Hz sine oscillator and connect it to output

engineStart();
sleep(0.5);

begin(0);
newNode("sinosc", 101);
setInput(101, 0, 440.0);
setInputX(101, 1, 0.2, 0.5, FadeCurve.fadeLinear);
connect(101, 0, 0, 0);
go();

sleep(3.0);

begin(0);
setInputX(101, 1, 0.0, 1.0, FadeCurve.fadeEaseOutCubic);
go();

sleep(1.5);
engineStop();
```
