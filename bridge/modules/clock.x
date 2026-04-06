-- clock.x
-- Extends the clock FFI module with coroutine scheduling.

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
