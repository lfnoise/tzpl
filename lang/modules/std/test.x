-- test.x -- assertion helpers with stable one-line output, composable with
-- the golden-file test runner and readable in the app's output panel.
--
--     PASS <label>
--     FAIL <label>: expected <x> got <y>
--
-- Counters live behind Refs so every function sees shared state (top-level
-- values are captured by value; the Ref pointer is the shared part).

let _passed = &0;
let _failed = &0;

fn _record(ok Bool, label String, detail String) Bool {
    if (ok) {
        _passed <- *_passed + 1;
        println("PASS " $ label);
    } else {
        _failed <- *_failed + 1;
        println("FAIL " $ label $ detail);
    }
    ok
}

fn assertEq<T>(actual T, expected T, label String) Bool {
    _record(actual == expected, label,
        ": expected " $ expected prettyString $ " got " $ actual prettyString)
}

fn assertTrue(cond Bool, label String) Bool = _record(cond, label, ": expected true");

fn assertFalse(cond Bool, label String) Bool = _record(!cond, label, ": expected false");

fn assertNear(actual Float, expected Float, eps Float, label String) Bool {
    _record(abs(actual - expected) <= eps, label,
        ": expected " $ expected prettyString $ " +- " $ eps prettyString
            $ " got " $ actual prettyString)
}

-- Run a named check; a false return (or a thrown trap) is a failure.
fn check(label String, f () Bool) Bool = _record(f(), label, ": check returned false");

-- Print a summary line and return the number of failures (0 = success).
fn testSummary() Int {
    println("%^ passed, %^ failed" fmt(*_passed, *_failed));
    *_failed
}
