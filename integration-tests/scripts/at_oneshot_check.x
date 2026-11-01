-- at()/after() are one-shot: the handler is Fn() Void, so the scheduler must
-- not read its return register as a reschedule delta (sched's convention).
-- The default-slot overloads used to omit oneShot, so a handler whose last
-- value happened to read as a positive Float re-fired at the same beat
-- forever, pinning the scheduler (it hung NRT renders and crashed live).
-- Each handler below ends in a Float-valued call to leave such a value
-- behind. Run via run_at_oneshot_test.sh.
import audio_engine.*;
import clock.*;
engineStart();

let nAt = &0;
let nAfter = &0;
let nAtC = &0;
let nAfterC = &0;
at(0.5, fn() Void { nAt <- *nAt + 1; getBeats(); });
after(0.5, fn() Void { nAfter <- *nAfter + 1; getBeats(); });
at(0, 0.5, fn() Void { nAtC <- *nAtC + 1; getBeats(); });
after(0, 0.5, fn() Void { nAfterC <- *nAfterC + 1; getBeats(); });

schedAbs(1.5, fn() Float {
    "AT ONESHOT counts %^ %^ %^ %^" fmt(*nAt, *nAfter, *nAtC, *nAfterC) println;
    if (*nAt == 1 && *nAfter == 1 && *nAtC == 1 && *nAfterC == 1) {
        "AT ONESHOT: OK" println;
    }
    engineStop();
    -1.0
});
