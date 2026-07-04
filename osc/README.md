# OSC (Open Sound Control) Module

This module provides Open Sound Control support for the Tzopilotl audio engine. It enables remote and local control of the audio engine via OSC messages, and exposes OSC functionality to the Tzopilotl programming language through the FFI bridge.

## Architecture

```
UDP Network
    |
    v
OscServer (listener thread, POSIX sockets)
    |
    v
OscDispatcher (address-pattern routing, mutex-protected)
    |
    +---> /engine/* handlers --> Engine (audio graph)
    +---> /reply/* responses --> OscClient (UDP sender)
    |
Tzopilotl Language (via FFI)
    |
    v
Bridge FFI (oscSend*, oscSendLocal*, oscServer*)
    |
    +---> OscClient  (remote UDP send)
    +---> OscDispatcher (local in-process dispatch)
```

### Components

- **OscServer** -- Spawns a thread that listens on a UDP socket (port configurable). Received packets are forwarded to the OscDispatcher.
- **OscClient** -- Sends OSC messages over UDP using oscpack for serialization.
- **OscDispatcher** -- Routes incoming OSC messages (and bundles) to registered handler functions by address pattern. Thread-safe via mutex.
- **Engine command handlers** -- A set of handlers registered on the dispatcher that translate OSC messages into engine API calls.

### Dependencies

- **oscpack** (vendored in `third_party/oscpack/`) -- C++ library for OSC packet construction and parsing. MIT licensed.
- **audio_engine_lib** -- The OSC engine commands depend on the engine's client API.

## Build

OSC support is optional. Enable it with:

```
cmake -DTZPL_BUILD_OSC=ON -DTZPL_BUILD_AUDIO_ENGINE=ON ..
```

Both flags are required since the OSC engine commands depend on the audio engine.

The build produces:
- `osc_lib` -- Static library with server, client, dispatcher, and engine command handlers
- `tzpl_osc_bridge` -- Object library with FFI bindings (built if `osc_lib` target exists)

## Application Setup

The application initializes OSC in this order:

```cpp
#include "tzpl_osc.hpp"
#include "tzpl_app_context.hpp"

osc::OscClient oscClient;
osc::OscDispatcher oscDispatcher;
oscDispatcher.setEngine(engine);
oscDispatcher.setClient(&oscClient);
osc::registerEngineHandlers(oscDispatcher);

osc::OscServer oscServer(oscDispatcher);
oscServer.start(port);   // begin listening
// ...
oscServer.stop();         // shutdown
```

Configuration options:
- Config file: `oscPort = <port>` (0 = disabled)
- CLI: `--osc-port <port>`

## Tzopilotl Language Interface

Import the OSC module in Tzopilotl code:

```
import osc.*;
```

### Available functions

**Remote send** (over UDP):

| Function | Signature |
|---|---|
| `oscSend` | `(host String, port Int, address String) Void` |
| `oscSendI` | `(host String, port Int, address String, value Int) Void` |
| `oscSendF` | `(host String, port Int, address String, value Float) Void` |
| `oscSendS` | `(host String, port Int, address String, value String) Void` |
| `oscSendArgs` | `(host String, port Int, address String, args Array[Float]) Void` |

**Local send** (in-process dispatch, bypasses the network):

| Function | Signature |
|---|---|
| `oscSendLocal` | `(address String) Void` |
| `oscSendLocalI` | `(address String, value Int) Void` |
| `oscSendLocalF` | `(address String, value Float) Void` |
| `oscSendLocalS` | `(address String, value String) Void` |
| `oscSendLocalArgs` | `(address String, args Array[Float]) Void` |

**Server control:**

| Function | Signature |
|---|---|
| `oscServerStart` | `(port Int) Bool` |
| `oscServerStop` | `() Void` |
| `oscServerPort` | `() Int` |

### Example

```
import osc.*;

-- Start an OSC server
oscServerStart(57120);

-- Send a message to another application
oscSend("127.0.0.1", 57110, "/hello");
oscSendF("127.0.0.1", 57110, "/synth/freq", 440.0);

-- Control the local engine without network overhead
oscSendLocal("/engine/startAudio");
oscSendLocalI("/engine/newNode", 1);

-- Clean up
oscServerStop();
```

## Engine OSC Commands

All engine commands use the `/engine/` address prefix. Query replies are sent back to the sender on `/reply/` addresses.

### Lifecycle and Configuration

| Address | Arguments | Description |
|---|---|---|
| `/engine/startAudio` | -- | Start audio output |
| `/engine/stopAudio` | -- | Stop audio output |
| `/engine/masterGain` | `float gain` | Set master output gain |
| `/engine/safetyLimiter` | `int enable` | Enable (1) or disable (0) the safety limiter |
| `/engine/loadDefs` | `string dirPath` | Load all plugin definitions from a directory |
| `/engine/loadDef` | `string path, string name` | Load a single plugin definition |

### Queries

Replies are sent back to the sender's address and port.

| Address | Reply Address | Reply Arguments | Description |
|---|---|---|---|
| `/engine/getStreamTime` | `/reply/getStreamTime` | `float time` | Current audio stream time |
| `/engine/listNodeDefs` | `/reply/listNodeDefs` | `string name...` | Names of all loaded node definitions |
| `/engine/isAudioRunning` | `/reply/isAudioRunning` | `int running` | Whether audio is currently running (0/1) |

### Graph Manipulation

These commands modify the audio graph. When sent individually (not inside an OSC bundle), they are automatically wrapped in a `begin()/go(silo)` transaction. When sent inside a bundle, multiple commands execute atomically.

| Address | Arguments | Description |
|---|---|---|
| `/engine/newNode` | `string defName, int nodeID` | Create a new node from a definition |
| `/engine/freeNode` | `int nodeID` | Destroy a node |
| `/engine/freeAllNodes` | -- | Destroy all nodes |
| `/engine/replaceNode` | `int oldID, int newID, float xfade, int curve` | Replace a node with crossfade |
| `/engine/connect` | `int srcID, int srcIdx, int dstID, int dstIdx` | Connect output to input |
| `/engine/connectX` | `int srcID, int srcIdx, int dstID, int dstIdx, float xfade, int curve` | Connect with crossfade |
| `/engine/disconnectInput` | `int nodeID, int idx` | Disconnect an input |
| `/engine/disconnectInputX` | `int nodeID, int idx, float xfade, int curve` | Disconnect input with crossfade |
| `/engine/disconnectOutput` | `int nodeID, int idx` | Disconnect an output |
| `/engine/disconnectNode` | `int nodeID` | Disconnect all connections on a node |
| `/engine/reconnectOutput` | `int oldSrcID, int oldSrcIdx, int newSrcID, int newSrcIdx, float xfade, int curve` | Redirect an output's connections to a new source |
| `/engine/setInput` | `int nodeID, int idx, float value` | Set a constant value on an input |
| `/engine/setInputX` | `int nodeID, int idx, float value, float xfade, int curve` | Set input with crossfade |
| `/engine/setControl` | `int nodeID, int ctrlID, float value` | Set a control parameter |

### Note Events

| Address | Arguments | Description |
|---|---|---|
| `/engine/noteOn` | `int nodeID, int noteID, float params...` | Trigger a note with optional parameters |
| `/engine/noteOff` | `int nodeID, int noteID` | Release a note |
| `/engine/allNotesOff` | `int nodeID` | Release all notes on a node |
| `/engine/noteSetParams` | `int nodeID, int noteID, int firstParam, float values...` | Update parameters on a running note |

## OSC Bundles

OSC bundles allow multiple commands to be sent atomically. The engine processes all commands in a bundle within a single `begin()` transaction, finalized with either `go(silo)` (immediate) or `sched(silo, ...)` (scheduled); the silo from the optional `/engine/silo` element is passed at finalize.

### Silo selection

The first element in a bundle can optionally be `/engine/silo <int>` to target a specific audio worker thread (silo). If omitted, silo 0 is used.

### Timetag scheduling

Bundle timetags determine how the command batch is finalized:
- Timetag <= 1 (including the "immediately" timetag): finalized with `go(silo)` for immediate execution
- Timetag > 1: interpreted as NTP timestamp, converted to engine stream time and finalized with `sched(silo, ...)` for scheduled execution

### Nested bundles

Bundles may be nested. Inner bundles are dispatched recursively with their own timetags.

## Testing

Integration tests are in `integration-tests/src/test_osc.cpp` and cover:
1. Server lifecycle (start/stop, port reporting)
2. Client message sending
3. Engine command dispatch
4. Local (in-process) dispatch
5. FFI registration and Tzopilotl language integration

A Tzopilotl test script is at `integration-tests/scripts/test_osc.x`.

Run the tests (requires `TZPL_BUILD_OSC=ON`):

```
cd build && ctest -R test_osc
```
