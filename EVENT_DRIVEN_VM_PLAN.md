# Event-Driven VM Plan

This document details the design for integrating the Tzopilotl VM into two fundamentally different execution environments: **real-time (RT) audio threads** and **non-real-time (NRT) threads**. These environments share the same VM core but differ in their event sources, constraints, and dispatch mechanisms.

**Last updated**: 2026-03-22

---

## 1. Overview: Two Environments, One VM

The Tzopilotl VM is designed to be event-driven: receive an event, execute a handler, stack collapses, return control to the host. This model maps cleanly onto both RT and NRT use cases, but the two environments have incompatible constraints that shape how events are delivered and processed.

```
                    ┌─────────────────────────────┐
                    │      Compiled Tzopilotl      │
                    │    (CodeBlocks, Globals,      │
                    │     Handler Functions)        │
                    └──────────────┬───────────────┘
                                   │
                    ┌──────────────┴───────────────┐
                    │         VM Core               │
                    │  (registers, TLSF, GC,        │
                    │   callFunction, dispatch)      │
                    └──────┬───────────────┬────────┘
                           │               │
              ┌────────────┴───┐     ┌─────┴─────────────┐
              │   RT VM        │     │   NRT VM           │
              │                │     │                    │
              │ Lives inside   │     │ Does NOT own a     │
              │ a Silo on an   │     │ thread. Protected  │
              │ audio worker   │     │ by a mutex.        │
              │ thread.        │     │                    │
              │                │     │ Called directly by  │
              │ Receives:      │     │ whatever thread    │
              │ engine cmds,   │     │ the event occurs   │
              │ scheduler      │     │ on: OSC server,    │
              │ events.        │     │ NATS client, UI,   │
              │                │     │ scheduler, etc.    │
              │                │     │                    │
              │ Cannot:        │     │ Can: allocate,     │
              │ block, do I/O. │     │ block, do I/O,     │
              │ TLSF may grow  │     │ call any function. │
              │ on exhaustion. │     │                    │
              └────────────────┘     └────────────────────┘
```

### Key Principle

Each VM instance is **serialized** -- at most one thread executes it at any time.

- **RT VMs** achieve this structurally: only the Silo's worker thread ever touches the VM. No mutex needed.
- **NRT VMs** achieve this via a **per-VM mutex**. Any thread (OSC server, NATS client, scheduler, UI) may call into the VM, but must hold the mutex while doing so.

This means the VM itself has no internal locking or atomics, which is critical for RT safety. The serialization guarantee is provided externally.

---

## 2. RT VM: Embedded in the Silo

### 2.1 Placement

Each Silo optionally owns one VM instance. The VM is created and initialized on an NRT thread, then handed to the Silo. Once attached, only the Silo's RT thread touches the VM.

```cpp
struct Silo {
    // ... existing fields ...
    ts::VM* vm_ = nullptr;              // optional attached VM
    CompileResult* vmProgram_ = nullptr; // currently installed program
};
```

The VM is allocated with a TLSF pool (e.g. 4-16 MB). If the pool is exhausted at runtime, the TLSF acquires more memory (from a pre-filled free-block queue or by calling the system allocator). This sacrifices real-time guarantees momentarily, but the rationale is: **it is better to glitch than to fail**. In a live performance, a brief audio dropout from a system allocation is far preferable to skipping an all-notes-off, missing a section trigger, or crashing. An aggressive GC sweep as an alternative could cause even longer degradation and still might not free enough memory in time.

### 2.2 When the RT VM Executes

The RT VM is **event-driven only**. It is not called per-sample or per-buffer. It executes when an event arrives from one of two sources:

1. **Command queue** -- a `VMEventCmd` arrives via the Silo's `from_nrt_` FIFO.
2. **Scheduling queue** -- a `VMEventCmd` fires at a specific sample time or tempo-based time via the Silo's hash-wheel scheduler.

Both paths use `VM::callFunction()` which has the right semantics: push a frame, dispatch, return when done. The stack is empty between calls.

A new command subclass `VMEventCmd` carries an event type tag and payload. It flows through the same path as all other engine commands:

```
NRT thread                           RT thread (Silo)
    │                                     │
    │  push VMEventCmd to from_nrt_       │
    │ ──────────────────────────────────>  │
    │                                     │ processRTCommands() or
    │                                     │ processScheduledEvents()
    │                                     │   cmd->run(silo)
    │                                     │     vm_->callFunction(handler, ...)
    │                                     │       ... handler executes ...
    │                                     │       stack collapses
    │                                     │     return
    │                                     │
```

This reuses the scheduler's sample-accurate timing, scheduling policies (`schedImmediate`, `schedBetterLateThanNever`, `schedOnTimeOnly`), and the two-stage command lifecycle.

The RT VM does not do DSP computation directly. Audio DSP is handled by compiled native plugins (via the synthdef-compiler). The RT VM's role is to respond to events by issuing **RT-safe engine commands only** -- setting controls, triggering notes (noteOn, noteOff, allNotesOff), and setting note parameters. Graph-building operations (newNode, connect, setInput, disconnect) cannot be initiated from the RT thread because they contain an NRT stage that allocates or deallocates memory -- these must originate from an NRT thread. Graph modification is the responsibility of the NRT VM.

### 2.3 GC Integration

The incremental GC is already designed for bounded pauses. On the RT thread:

- **`gcHeartbeat()`** is called after each event handler returns (inside `VMEventCmd::doRT()`). Since the VM is event-driven, there is no per-buffer opportunity -- heartbeats are driven by event activity.
- If events are infrequent, GC pressure is also low (no allocations happening between events), so the lag is acceptable.
- Between events, only globals and coroutine frames are GC roots (the call stack is collapsed), which keeps root-scanning fast.
- The heartbeat budget is tuned to complete a full GC cycle within a reasonable number of events.

### 2.4 RT Safety Enforcement

The compiler already supports an `rt_restricted` flag on compilation targets. When enabled, the type checker rejects calls to functions not marked `rt_safe`. This is the gate for what code can run on an RT VM:

- All handler functions destined for RT execution must be compiled with `rt_restricted = true`.
- The 19 engine FFI functions already marked `rtSafe` are callable.
- User-defined pure functions (no I/O, no system allocation) are callable.
- Functions that allocate GC objects are callable (TLSF is RT-safe).
- Functions that call `print`, file I/O, or non-RT FFI functions are rejected at compile time.

### 2.5 Bounded Execution

Unbounded loops on the RT thread would cause audio dropouts. Two approaches, which can be combined:

1. **Instruction budget**: The VM counts instructions executed and halts if a budget is exceeded. This adds a check per instruction (cheap but nonzero). The budget is configurable per VM.
2. **Static analysis**: The compiler rejects unbounded loops (`while`, recursion without base case) in RT-restricted code, permitting only bounded `for` loops over known-size ranges. This is more restrictive but has zero runtime cost.

The initial implementation should use approach 1 (instruction budget) as it is simpler and catches all cases. Approach 2 can be added later as an optimization that removes the per-instruction check.

### 2.6 What the RT VM Cannot Do

- Call blocking system calls (enforced by `rt_restricted` compilation)
- Perform I/O (enforced by `rt_restricted` compilation)
- Run unbounded computations (enforced by instruction budget)
- Be accessed from any thread other than its Silo's worker thread

Note: The TLSF allocator may grow on exhaustion by acquiring memory from the system allocator. This is a deliberate trade-off -- a brief RT violation is preferable to failure (see section 10, decision 1).

---

## 3. NRT VM: Mutex-Serialized, No Dedicated Thread

### 3.1 Architecture

An NRT VM does **not** own a thread. Instead, it is a mutex-protected resource that any NRT thread can call into directly. The caller's thread acquires the VM's mutex, sets the thread-local `gCurrentVM`, calls `vm->callFunction()`, runs a GC heartbeat, and releases the mutex. The VM executes on whatever thread triggered the event.

```
┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│  OSC Server  │  │ NATS Client  │  │   UI Thread   │  │  Scheduler   │
│   Thread     │  │   Thread     │  │              │  │   Thread     │
└──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘
       │                 │                 │                 │
       │  lock(vm.mtx)   │                 │                 │
       │  callFunction() │                 │                 │
       │  gcHeartbeat()  │                 │                 │
       │  unlock(vm.mtx) │                 │                 │
       │                 │                 │                 │
       │                 │  lock(vm.mtx)   │                 │
       │                 │  callFunction() │                 │
       │                 │  gcHeartbeat()  │                 │
       │                 │  unlock(vm.mtx) │                 │
       │                 │                 │                 │
       v                 v                 v                 v
                    ┌──────────────────────────┐
                    │       NRT VM             │
                    │  (mutex-serialized)       │
                    │                          │
                    │  At most one thread      │
                    │  executes at a time.     │
                    │  No dedicated thread.    │
                    └──────────────────────────┘
```

### 3.2 VM Wrapper

The NRT VM is wrapped in a struct that provides the mutex and the calling convention:

```cpp
struct NRTVM {
    ts::VM vm;
    std::mutex mtx;
    HandlerTable handlers;

    // Call a handler under the mutex. Any thread may call this.
    Word call(CodeBlock* block, const Word* args, u16 argc) {
        std::lock_guard lock(mtx);
        vm.makeCurrent();  // set thread-local gCurrentVM
        Word result = vm.callFunction(block, args, argc);
        vm.gcHeartbeat();
        return result;
    }

    // Compile and install new code under the mutex.
    void installCode(CompileResult* result) {
        std::lock_guard lock(mtx);
        vm.makeCurrent();
        vm.install(result);
    }
};
```

Each event source (OSC server, NATS client, scheduler, UI) calls `nrtVM.call()` directly on its own thread. The mutex ensures the VM is never entered concurrently. If a second event arrives while the VM is busy, the second caller blocks on the mutex until the first completes -- this is acceptable for NRT operation.

### 3.3 Direct Dispatch Examples

**OSC server thread** receives a message, looks up the handler, calls the VM:
```cpp
// In OscDispatcher, on the OSC server thread:
void dispatchToVM(NRTVM& nrtVM, const std::string& address,
                  const osc::ReceivedMessage& msg) {
    CodeBlock* handler = nrtVM.handlers.oscHandlers[address];
    if (handler) {
        Word args[16];
        u16 argc = packOscArgs(msg, args);  // convert OSC args to Words
        nrtVM.call(handler, args, argc);
    }
}
```

**NRT scheduler thread** fires a timed event, calls the VM:
```cpp
// On the scheduler's own thread:
void NRTScheduler::fireEvent(NRTVM& nrtVM, Entry& entry) {
    nrtVM.call(entry.handler, entry.args.data(), entry.argc);
}
```

**UI thread** evaluates user code:
```cpp
// On the UI thread:
void evalUserCode(NRTVM& nrtVM, const std::string& code) {
    std::lock_guard lock(nrtVM.mtx);
    nrtVM.vm.makeCurrent();
    // compile and execute under the same lock
    auto result = compiler.compile(code);
    nrtVM.vm.install(result);
    nrtVM.vm.execute(result->mainBlock);
    nrtVM.vm.gcHeartbeat();
}
```

### 3.4 NRT Scheduler

The NRT scheduler runs on its own thread and fires wall-clock-timed events. It does not live inside the VM -- it is an external service that calls into the VM when events are due.

```cpp
struct NRTScheduler {
    struct Entry {
        TimePoint when;
        CodeBlock* handler;
        std::vector<Word> args;
        u16 argc;
        bool repeating;
        Duration interval;
    };

    std::mutex schedMtx;              // protects the queue
    std::condition_variable cv;
    std::priority_queue<Entry> queue_;
    NRTVM* vm_;                       // the VM to call into
    bool running_ = false;

    void run() {
        while (running_) {
            std::unique_lock lock(schedMtx);
            if (queue_.empty()) {
                cv.wait(lock);
                continue;
            }
            auto next = queue_.top();
            if (next.when > Clock::now()) {
                cv.wait_until(lock, next.when);
                continue;
            }
            queue_.pop();
            lock.unlock();

            // Call the VM on the scheduler thread
            vm_->call(next.handler, next.args.data(), next.argc);

            if (next.repeating) {
                next.when += next.interval;
                std::lock_guard lock2(schedMtx);
                queue_.push(next);
            }
        }
    }
};
```

Tzopilotl code schedules NRT events via FFI:
```
fn after(seconds Float, handler Fn() Void) Void;
fn every(seconds Float, handler Fn() Void) Void;
fn cancel(timerID Int) Void;
```

For sample-accurate scheduling on the engine, Tzopilotl code continues to use the existing `sched()` FFI function, which pushes commands through the engine's RT scheduler.

### 3.5 What the NRT VM Can Do

- Allocate from the system allocator (though TLSF is still used by default for performance)
- Perform I/O (file reads, print, network)
- Call any FFI function (both RT-safe and non-RT-safe)
- Block (the calling thread blocks, but other threads wait on the mutex -- acceptable for NRT)
- Run unbounded computations (no instruction budget)
- Send commands to the engine (via `begin()`/`go()`/`sched()`)
- Compile code (compilation happens under the mutex on the calling thread)

### 3.6 Considerations

**Mutex contention**: If the VM is executing a long handler when another event arrives, the second event's thread blocks. This is fine for sporadic events (OSC, user input). If high-frequency events cause contention, the handler code should be kept short, with heavy work delegated to the engine via scheduled commands.

**Thread-local state**: The VM uses `thread_local` globals (`gCurrentVM`, `gCurrentTypeUniverse`). Since any thread may call the VM, `makeCurrent()` must be called under the mutex before each VM invocation to set these correctly for the calling thread.

**GC heartbeats**: Each `call()` invocation runs one GC heartbeat. Since the VM has no dedicated thread, there is no idle-time GC. If events are infrequent, the GC may lag. This is acceptable -- the GC is incremental and will catch up when events resume. If needed, a periodic keepalive timer in the NRT scheduler can drive heartbeats.

---

## 4. Event Types and Routing

### 4.1 Event Sources and Destinations

| Event Source | Destination | Mechanism |
|---|---|---|
| OSC message | NRT VM | OSC server thread acquires VM mutex, calls handler directly |
| OSC message | Engine (direct) | OSC dispatcher calls engine commands (already implemented) |
| NATS message | NRT VM | NATS client thread acquires VM mutex, calls handler directly |
| User input (REPL/editor) | NRT VM | UI thread acquires VM mutex, compiles/executes directly |
| NRT timer | NRT VM | Scheduler thread acquires VM mutex, calls handler directly |
| Engine scheduler (sample-accurate) | RT VM | Existing command/scheduler path via `VMEventCmd` |
| Engine command (immediate) | RT VM | Existing `from_nrt_` FIFO via `VMEventCmd` |
| Note on/off | RT VM | New command subclass `VMNoteCmd` routed to VM handler |
| Control change | RT VM | New command subclass `VMControlCmd` routed to VM handler |
| Code update | RT | `CodeInstallCmd` via `from_nrt_` FIFO |
| Code update | NRT | Any thread acquires VM mutex, calls `installCode()` |

### 4.2 OSC Routing to the NRT VM

The existing `OscDispatcher` routes OSC addresses to handler functions. For VM integration, we add a new category of OSC handlers that push events to the NRT VM's queue rather than calling engine commands directly:

```
/tzpl/eval <code_string>          -- evaluate arbitrary code
/tzpl/call <func_name> [args...]  -- call a named function
/tzpl/<user_address> [args...]    -- call a user-registered handler
```

User code registers OSC handlers from Tzopilotl:
```
import osc;

fn myHandler(args Array[Float]) Void {
    let freq = args[0];
    begin(0);
    setInput(1, 0, freq);
    go();
}

osc.onMessage("/synth/freq", myHandler);
```

The `osc.onMessage` FFI function registers the handler in the `OscDispatcher` and associates it with the NRT VM. When an OSC message arrives at that address, the dispatcher acquires the NRT VM's mutex and calls the handler directly on the OSC server thread. The mutex ensures serialization -- if the VM is already busy handling another event on a different thread, the OSC thread blocks until it completes.

### 4.3 RT Event Dispatch via Commands

RT events are delivered as engine commands. New command subclasses:

```cpp
// Dispatches an event to the Silo's attached VM
struct VMEventCmd : Command {
    enum EventKind { Timer, NoteOn, NoteOff, ControlChange, Custom };
    EventKind kind_;
    CodeBlock* handler_;   // the handler function to call
    Word args_[8];         // inline argument storage (no allocation)
    u16 argc_;

    void doRT(Silo* s) override {
        if (s->vm_ && handler_) {
            s->vm_->callFunction(handler_, args_, argc_);
        }
    }
};
```

These commands are created on NRT threads and sent through the existing `from_nrt_` FIFO, optionally via the scheduler for timed execution. They participate in the two-stage command lifecycle -- stage 1 (`doRT`) runs the handler, stage 2 (`doNRT`) can clean up.

---

## 5. Handler Registration

### 5.1 From Tzopilotl Code

Handlers are registered via FFI functions that bind Tzopilotl functions to specific event types:

```
-- NRT handlers (registered on the NRT VM)
osc.onMessage("/my/address", handler);   -- OSC pattern -> handler
nats.onMessage("subject.>", handler);    -- NATS subject -> handler
after(2.0, handler);                     -- wall-clock timer
every(0.5, handler);                     -- repeating timer

-- RT handlers (registered on a Silo's VM)
-- These must be compiled with rt_restricted = true
rt.onNote(noteOnHandler, noteOffHandler);  -- polyphonic note events
rt.onControl(controlID, handler);          -- control parameter changes
```

### 5.2 Handler Table

Each VM maintains a handler table mapping event types to CodeBlock pointers:

```cpp
struct HandlerTable {
    // NRT handlers
    Map<std::string, CodeBlock*> oscHandlers;     // OSC address -> handler
    Map<std::string, CodeBlock*> natsHandlers;    // NATS subject -> handler

    // RT handlers
    CodeBlock* noteOnHandler = nullptr;           // note-on event
    CodeBlock* noteOffHandler = nullptr;          // note-off event
    Map<i64, CodeBlock*> controlHandlers;         // control ID -> handler

    // Shared
    Vec<TimerEntry> timers;                       // scheduled callbacks
};
```

On the RT side, the handler table is part of the Silo's VM state. Updates to the handler table (e.g., registering a new handler) are delivered as commands through the `from_nrt_` FIFO, ensuring the RT thread never sees a partially-updated table.

### 5.3 Coroutines as RT Handlers

Coroutines are permitted on both RT and NRT VMs. On the RT side, coroutines are particularly useful for scheduling ongoing tasks. A handler can create or resume a coroutine that yields between steps, with each resumption triggered by a subsequent event (e.g., a scheduler tick). This allows multi-step sequences without blocking:

```
-- RT handler that drives a coroutine one step per event
fn stepSequencer(coro Coroutine[Void, Void]) Void {
    resume(coro);
}
```

Since coroutine frames are GC-managed objects, they persist in the VM's heap between events and are scanned as GC roots alongside globals.

---

## 6. Code Hot-Reload

Both RT and NRT VMs must support installing new code while running. The mechanism differs by environment but the semantics are the same: replace handler function pointers and global variable definitions without stopping the VM.

### 6.1 What Gets Updated

A code update consists of a new `CompileResult` containing:
- New or updated `CodeBlock`s (function bodies)
- New or updated global variable definitions
- New or updated handler registrations

The update does **not** wipe existing global variable values unless the variable is new. Existing globals retain their current values. This allows state to persist across hot-reloads.

### 6.2 NRT Code Install

On the NRT VM, code installation happens under the mutex on whatever thread initiates it:

1. The calling thread acquires the VM mutex.
2. The compiler runs, producing a new `CompileResult`.
3. `vm.install(newCompileResult)` updates the VM's function table and global slots.
4. Handler registrations in the new code take effect immediately.
5. Old `CodeBlock` objects become unreferenced and are collected by the GC.
6. The mutex is released.

This is typically triggered by the UI thread (user edits code) or by an OSC `/tzpl/eval` message on the OSC server thread. In either case, the calling thread holds the mutex for the duration of both compilation and installation, ensuring no other event can execute on the VM mid-update.

### 6.3 RT Code Install

On the RT VM, code cannot be compiled on the RT thread (compilation allocates from the system allocator). Instead:

1. Code is compiled on an NRT thread.
2. A `CodeInstallCmd` is created containing the new `CompileResult`.
3. The command is pushed to the Silo's `from_nrt_` FIFO.
4. When the Silo processes the command on the RT thread, it calls `vm_->install(newCompileResult)`.
5. Old CodeBlocks are pushed to the `dead_nodes_` FIFO (or a similar deferred-deletion mechanism) for cleanup on the NRT thread.

```cpp
struct CodeInstallCmd : Command {
    CompileResult* newCode_;

    void doRT(Silo* s) override {
        if (s->vm_) {
            auto old = s->vm_->install(newCode_);
            // old CodeBlocks are now unreferenced;
            // GC will collect them, deletion deferred to NRT
        }
    }

    bool doNRT(Silo* s) override {
        // newCode_ ownership transferred to VM; nothing to clean up here.
        return true;
    }
};
```

### 6.4 Atomic Handler Swap

When a handler is updated via hot-reload, the swap must be atomic with respect to event dispatch. Since the RT VM is single-threaded, this is naturally atomic -- the `CodeInstallCmd::doRT()` runs between events, never during one. The old handler pointer is simply overwritten.

---

## 7. Communication Between RT and NRT VMs

### 7.1 NRT to RT

The NRT VM sends commands to the engine (and thus to RT Silos) via the existing engine client API (`begin()`/`go()`/`sched()`). This is already implemented in the FFI bridge. No new mechanism is needed.

For VM-specific communication (e.g., NRT VM wants to trigger a handler on an RT VM), a `VMEventCmd` is created and sent through the same FIFO:

```
NRT VM thread:
    begin(silo);
    // ... build a VMEventCmd ...
    sched(time, policy);
```

### 7.2 RT to NRT

The RT VM cannot acquire the NRT VM's mutex (that would block the RT thread). Instead, RT-to-NRT communication goes through the engine's existing `to_nrt_` FIFO:

1. During an RT handler, a Tzopilotl FFI function creates a `VMReplyCmd` containing a handler reference and arguments.
2. The command is pushed to `to_nrt_` (lock-free, RT-safe).
3. The engine's NRT command processing thread picks it up, acquires the NRT VM's mutex, and calls the handler.

```
RT VM (Silo)                        NRT command thread         NRT VM
    │                                     │                      │
    │ push VMReplyCmd to to_nrt_          │                      │
    │ ──────────────────────────────────> │                      │
    │                                     │ lock(nrtVM.mtx)      │
    │                                     │ callFunction(...)    │
    │                                     │ gcHeartbeat()        │
    │                                     │ unlock(nrtVM.mtx)    │
    │                                     │                      │
```

This means the engine's NRT command thread becomes another caller of the NRT VM, just like the OSC server or scheduler threads. The mutex serializes it with all other callers.

### 7.3 Shared State

For high-frequency data sharing (e.g., RT VM exposes current amplitude to NRT VM for visualization), use a lock-free shared buffer:

- RT VM writes to a double-buffered or atomic value.
- NRT VM reads the latest value.
- No FIFO overhead for data that is overwritten each buffer.

This is an optimization for specific use cases (metering, visualization) and is not part of the core event system.

---

## 8. VM Lifecycle

### 8.1 Creation

```
1. Allocate TLSF pool (size depends on RT vs NRT)
2. Create VM with pool
3. Create Compiler, create Target (rt_restricted for RT VMs)
4. Compile initial code
5. Install CompileResult into VM
6. Register FFI functions (engine bridge, OSC bridge, etc.)
7. For RT: attach VM to Silo via command
   For NRT: register with event sources (OSC dispatcher, scheduler, etc.)
```

### 8.2 Steady State

```
RT VM:
    Event-driven within the Silo's processFrames() loop:
    - processRTCommands() dispatches VMEventCmds from the command queue
    - processScheduledEvents() dispatches timed VMEventCmds from the scheduler
    - gcHeartbeat() called after each event handler returns
    - VM is idle between events (no per-sample or per-buffer calls)

NRT VM:
    No dedicated thread. Various threads call in:
    - OSC server thread: acquires mutex, calls handler, runs gcHeartbeat()
    - NATS client thread: acquires mutex, calls handler, runs gcHeartbeat()
    - NRT scheduler thread: acquires mutex, calls handler, runs gcHeartbeat()
    - UI thread: acquires mutex, compiles/executes, runs gcHeartbeat()
    - Engine NRT cmd thread: acquires mutex for RT-to-NRT replies
```

### 8.3 Shutdown

```
RT VM:
    1. Send a command to detach VM from Silo
    2. Silo nulls its vm_ pointer on RT thread
    3. VM is deleted on NRT thread (via dead_nodes_ or similar FIFO)

NRT VM:
    1. Unregister from all event sources (OSC dispatcher, scheduler, etc.)
    2. Acquire mutex one final time to ensure no handler is running
    3. Delete VM
```

---

## 9. Implementation Phases

### Phase A: NRT VM with Mutex Serialization

**Why first**: The NRT VM is simpler (no RT constraints) and enables the most immediate user-facing features (OSC-driven live coding, REPL integration).

1. Implement `NRTVM` wrapper struct with mutex and `call()` method.
2. Integrate with existing OSC server: OSC dispatcher acquires VM mutex and calls handlers directly on the OSC server thread.
3. Implement `osc.onMessage()` FFI for user handler registration.
4. Implement NRT scheduler (own thread, calls into VM via mutex).
5. Implement `after()` / `every()` NRT timer FFI functions.
6. Integrate with the app: the CLI app creates an NRT VM, UI thread calls into it for REPL evaluation.
7. Implement code hot-reload for NRT (compile and install under mutex, old code collected by GC).

### Phase B: RT VM on Silo

**Why second**: Requires Phase A for the compilation pathway (code is compiled on NRT, sent to RT).

1. Add `vm_` and `HandlerTable` to `Silo`.
2. Implement `VMEventCmd` command subclass.
3. Implement `CodeInstallCmd` for RT code hot-reload.
4. Implement `rt.onNote()`, `rt.onControl()` FFI functions.
5. Add instruction budget to the VM for bounded RT execution.
6. Extend TLSF to grow on exhaustion (acquire free block from queue or system allocator).
7. Test: compile an RT-restricted handler on NRT, send via FIFO, execute on RT, verify audio output.

### Phase C: Cross-VM Communication

**Why third**: Refinement layer on top of working RT and NRT VMs.

1. Implement `VMReplyCmd` for RT-to-NRT messaging.
2. Implement shared lock-free buffers for metering/visualization data.
3. Implement `nats.onMessage()` FFI (depends on NATS integration, Phase 6 of main plan).
4. Test: NRT VM sends a scheduled command to RT, RT handler modifies graph, RT sends reply to NRT.

---

## 10. Design Decisions (Resolved)

1. **Pool exhaustion on RT**: The TLSF allocator will be extended to grow on demand -- acquiring a free block from a pre-filled queue or calling the system allocator. This briefly sacrifices real-time guarantees, but a glitch is preferable to failure. In a live performance, skipping an all-notes-off or a section trigger could be catastrophic. An aggressive GC sweep is not a viable alternative -- it may cause worse degradation and still might not free enough memory.

2. **Multiple NRT VMs**: One NRT VM per application for now. The architecture supports multiple VMs if needed later.

3. **Coroutines**: Allowed on both RT and NRT VMs. Coroutines are important for scheduling ongoing tasks (e.g., multi-step sequences where each step is triggered by an event). Suspended coroutine frames are GC-managed objects that persist in the heap between events and are scanned as GC roots alongside globals.

4. **RT VM execution model**: The RT VM is event-driven only -- it is NOT called per-sample or per-buffer. It executes when an event arrives from the command queue or from a sample-based or tempo-based scheduling queue. Audio DSP is handled by compiled native plugins; the VM orchestrates the audio graph by issuing engine commands in response to events.

5. **Mutex contention on NRT**: Unlikely to be an issue in practice since NRT events (OSC, user input, timers) are sporadic. If it ever becomes a bottleneck, one or more additional NRT VMs dedicated to background tasks can be created, with work queued onto them.
