-- rtonly negative half: silo-only functions must be COMPILE errors on the
-- NRT VM (run by run_rtonly_test.sh, which asserts this script fails to
-- compile with the "silo-only" diagnostic).
--
-- playNote is rtOnly directly; spawn(clock, coro) is an audio_engine.x
-- wrapper tainted transitively (its body calls the rtOnly scheduleTask).
import audio_engine.*;

coro fn tick() Float { yield 1.0; }
let id = spawn(0, tick());
playNote(101, 0, [60.0, 0.5]);
println("UNREACHABLE: rtOnly enforcement failed");
