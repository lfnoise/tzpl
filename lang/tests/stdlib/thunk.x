-- std.thunk: memoized lazy values.
import std.thunk.*;

-- Count evaluations through a Ref to show force() memoizes.
let calls = &0;
let t = thunk(fn() Int {
    calls <- *calls + 1;
    6 * 7
});

*calls println;      -- 0: not yet forced
t force println;     -- 42
t force println;     -- 42 (cached)
*calls println;      -- 1: evaluated exactly once

let s = thunk(fn() String { "ok" });
s force println;
