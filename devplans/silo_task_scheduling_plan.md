# Deferred plan: per-silo task scheduling

NOTE: this is NOT strongly-timed and is unrelated to ChucK's model. The
orchestration layer uses async/await (which ChucK does not have); silo tasks
are beat-scheduled coroutines on a silo's TempoClock.

Status: **DEFERRED** (parked 2026-06-24). Blocked on / waiting for the
async/await language feature, which is being implemented first so the
orchestration ergonomics are right from the start.

Builds on the synchronized TempoClock work already shipped (Phases 1-2,
commit a24f336): N beat-based TempoClock slots per silo, synced across silos;
clock.setTempo unified onto the engine clock.

## Goal

ChucK-style concurrency: tasks (coroutines) that run on a silo's RT VM, are
scheduled on that silo's TempoClock by beat, resume on the audio thread exactly
at their beat, emit events (notes/controls) sample-accurately, and advance time
by yielding beat-deltas. The same task code must run identically for real-time
and non-real-time (offline) rendering.

| ChucK | here |
|---|---|
| the VM on the audio thread | a silo's RT VM (`SiloVMState.vm`, `attachVM`) |
| a shred | a `coro fn` scheduled on a silo TempoClock |
| `1::beat => now` | `yield 1.0` (a beat-delta) |
| sample-accurate time | `TempoClock::process` (runs in both `processFrames` live and `renderNRTBlock` offline) |

Payoff: task code is byte-identical RT vs NRT; only the launch wrapper differs,
and NRT becomes sample-accurate/deterministic.

## Two layers + RT-safety boundary

- **Orchestration layer (main/NRT VM):** builds graphs (`newNode`/`connect` --
  these allocate, so they live here), sets global tempo (broadcast, synced),
  launches silo tasks, drives NRT renders.
- **Task layer (silo RT VM, audio thread):** allocation-free events only
  (`noteOn`/`noteOff`/`setControl`/`allNotesOff`), control flow, `yield`.
  Lang-object allocation in the silo VM is fine (TLSF); engine node allocation
  is NOT RT-safe, so it stays in the orchestration layer.

## Synchronization model (the hard part)

`siloLoad` is asynchronous (compile off-thread, install via command to the silo
RT thread). Putting `spawn` at a loaded module's top level ties **task start**
to **load completion**, which is nondeterministic -> parts start on different
beats. Two distinct problems:

1. **Load-completion barrier** -- know all setup (graph + code install) landed.
2. **Synchronized beat-start** -- even after loading, all tasks must *begin on a
   common beat*. Awaiting loads is not enough by itself.

Fix: **separate load from start.** `siloLoad` installs *definitions only* and
never auto-spawns. A separate, beat-anchored, broadcast start spawns the tasks
at one chosen beat across all silos.

RT/NRT asymmetry:
- **NRT:** the render setup closure runs to completion before the audio clock
  advances -> awaiting loads in setup makes beat 0 a perfect, deterministic
  barrier.
- **Live:** the clock is always ticking; need the completion gate AND a
  beat-quantized start (e.g. next bar) with enough lead for loads to land.

The barrier + synchronized start are the same primitives in both the
handle/callback and async/await styles; async/await (being implemented first)
is sugar over the same completion handles, so no rework.

### Corrected example shape

Silo module (installed on a silo VM) -- defines tasks + a `start()` entry; no
top-level spawn:
```tzpl
import silo.*;
let scale = [36.0, 38.0, 41.0, 43.0];
coro fn bassline() Float {
    var i = 0;
    while (true) {
        noteOn(10, i % 64, [scale[i % 4], 0.8]); yield 0.75;
        noteOff(10, i % 64);                     yield 0.25;
        i = i + 1;
    }
}
fn start() Void { spawn(0, bassline()); }   -- called on the synchronized downbeat
```

Orchestration (async/await form, once that feature exists):
```tzpl
async fn startSession() Void {
    await awaitAll([prepare(0, "bassVoicer", 10, "bass_task"),
                   prepare(1, "padVoicer",  20, "pad_task")]);
    setTempo(0, 96.0);
    siloStartAt(quantizeUp(clockBeats(0), 4.0), [0, 1]);  -- one synced downbeat
}
```

## New API surface

Silo-side module `silo.*` (inside a silo VM; targets the running silo):
- `spawn(clock Int, body Coroutine<Float>) Int`, `cancelTask(id Int)`
- `noteOn/noteOff/setControl/allNotesOff/noteSetParams` applied to *this* silo
  immediately (no begin/sched -- already on the silo at the right beat)
- `getBeat(clock Int) Float`

Orchestration:
- `attachVM/detachVM/siloLoad/siloEval` made **render-context-aware** (target the
  render engine's silos inside a render); each returns a completion handle.
- `awaitAll([h...])` / `onLoaded(h, cb)` / `whenAllLoaded(...)` barrier (or
  async/await once available).
- `siloStartAt(beat, [silos])` -- broadcast, beat-scheduled invocation of each
  silo's `start()` at a common beat.
- Existing Phase-1 `setTempo/schedTempoChange/sched/begin/go` unchanged.

## Implementation notes

1. **Pooled, self-rescheduling task entries (RT-safe).** Tasks are scheduled and
   rescheduled from the RT thread, so no `new`. Revive the `RTTempoScheduler`-style
   pre-allocated entry pool (retired in Phase 1) but drive it off the engine
   TempoClock ramp. `TempoClock::process` services both the Phase-1 heap-bundle
   queue and a pooled task list of `(beatTime, handler)` where the handler is a
   `fn() Float` returning the next beat-delta (`-1`/None => stop) -- exactly the
   old `processSample` reschedule logic, now sample-driven and synced.
2. **`spawn` FFI (silo-side):** wrap the coroutine in the Float-trampoline
   `clock.go` uses; needs a "current silo" context on the silo VM.
3. **Silo-side event FFI:** `noteOn/...` bound to the current silo, applied
   synchronously on the RT thread (allocation-free engine calls only).
4. **GC roots over silo task queues:** each silo VM registers a root scanner that
   marks the live trampoline/coroutine `Obj*` held in its clocks' task queues
   (mirrors the old `RTTempoScheduler::markRoots`).
5. **Render-context-aware `attachVM`/`siloLoad`:** set up per-silo VMs + GC roots
   on the render engine; tear down at render end. Unlocks NRT parity.
6. **Lifetime / cancellation:** stable task ids; `cancelTask` removes the pool
   entry; silo VM + tasks torn down on `detachVM` / render completion.

## Decisions to confirm when resumed

- Authoring model: silo task code is separate code compiled onto the silo VM
  (heaps differ; a main-VM closure can't run on a silo). Confirmed direction.
- RT-safety boundary: node allocation = orchestration; tasks = allocation-free
  events. Enforce.
- Tempo is global (broadcast/synced); a task can drive its own silo's clock but
  that intentionally de-syncs that slot.
- Start mechanism: explicit `start()` entry + `siloStartAt` vs a clock
  "transport" play-head (`spawn` arms; `startTransportAt(beat)` releases all).

## Milestones

- M1: make a main-VM coroutine able to schedule late-bound bundles onto any
  silo's clock (small; mostly composes with Phase 1).
- M2: real silo tasks -- pooled task entries on TempoClock, `spawn` + silo-side
  events + GC roots. Live/RT only first.
- M3: render-context-aware `attachVM`/`siloLoad` -> NRT parity.
- M4: ergonomics -- `silo`/`task` std module, `cancelTask`, multi-clock tasks,
  `playFor`-style helpers, the synchronization helpers above.
