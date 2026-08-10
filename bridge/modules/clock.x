-- clock.x
-- Script wrapper for the clock FFI. Re-exports `clock_ffi.*` and adds a
-- coroutine scheduling helper on top.

export clock_ffi.*;

-- Run a coroutine on the tempo scheduler.
-- The coroutine yields Float values (beat deltas).
-- After each yield, the coroutine is rescheduled that many beats later.
-- When the coroutine finishes, scheduling stops.
fn go(c Coroutine<Float>) Int {
    sched(0.0, fn() Float {
        let result = c next;
        if (result isSome) { result unwrap } else { -1.0 }
    })
}

-- Same, on engine TempoClock slot `clock` (synchronized across silos).
fn go(clock Int, c Coroutine<Float>) Int {
    sched(clock, 0.0, fn() Float {
        let result = c next;
        if (result isSome) { result unwrap } else { -1.0 }
    })
}
