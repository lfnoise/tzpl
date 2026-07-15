-- Regression guard for the `try` lowering's register bookkeeping: run many
-- ok/err propagations with freshly-allocated String payloads under allocation
-- pressure so GC safepoints fire while try results and propagated errors are
-- in flight. A stale error-path register in a later stack map (or an
-- untracked payload) shows up here as a crash or corrupted output.
import std.result.*;

fn classify(n Int) Result<String, String> {
    (n % 7) == 0 ? Result<String, String>.err("boom " $ toString(n))
                 : Result<String, String>.ok("val " $ toString(n))
}

fn step(n Int) Result<String, String> {
    let a = classify(n) try;              -- err every 7th
    let b = classify(n + 1) try;
    Result<String, String>.ok(a $ "/" $ b)
}

var oks = 0;
var errs = 0;
var i = 0;
while (i < 60000) {
    -- allocation pressure: a transient array of strings per iteration
    let junk = ["x" $ toString(i), "y" $ toString(i), "z" $ toString(i)];
    match (step(i)) {
        ok(v): { oks = oks + ((v length) > 0 ? 1 : 0); };
        err(m): { errs = errs + ((m length) > 0 ? 1 : 0); };
    }
    i = i + 1;
}

oks println;    -- 60000 - errs
errs println;   -- ceil: every n with n%7==0 or (n+1)%7==0
