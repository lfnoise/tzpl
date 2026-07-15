-- std.test: assertion helpers and summary output.
import std.test.*;

assertEq(1 + 1, 2, "ints add");
assertEq("a" $ "b", "ab", "strings concat");
assertEq([1, 2] $ [3], [1, 2, 3], "arrays concat");
assertTrue(3 > 2, "comparison");
assertFalse(2 > 3, "negative comparison");
assertNear(0.1 + 0.2, 0.3, 1e-9, "float near");
check("check passes", fn() Bool { true });

-- Deliberate failures to pin the FAIL format.
assertEq(1, 2, "deliberate mismatch");
assertTrue(false, "deliberate false");
assertNear(1.0, 2.0, 0.5, "deliberate far");
check("deliberate check", fn() Bool { false });

let failures = testSummary();
println(failures);
