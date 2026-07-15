-- Postfix `try` on Result<T, E>: unwraps ok, early-returns err.
import std.result.*;

fn parsePositive(s String) Result<Int, String> {
    match (parseInt(s)) {
        Option.some(n): n >= 0 ? Result<Int, String>.ok(n)
                               : Result<Int, String>.err("negative: " $ s);
        Option.none: Result<Int, String>.err("not a number: " $ s);
    }
}

fn report(r Result<Int, String>) Void {
    match (r) {
        ok(n): println(n);
        err(m): println(m);
    }
}

-- Two tries in one function; both ok
fn addParsed(a String, b String) Result<Int, String> {
    let x = parsePositive(a) try;
    let y = parsePositive(b) try;
    Result<Int, String>.ok(x + y)
}

report(addParsed("2", "3"));
report(addParsed("2", "x"));    -- second try propagates err
report(addParsed("-1", "3"));   -- first try propagates err

-- try inside a larger expression
fn plusTen(s String) Result<Int, String> {
    Result<Int, String>.ok(parsePositive(s) try + 10)
}
report(plusTen("5"));
report(plusTen("nope"));

-- Mid-pipeline chaining: try then a space-pipeline send
fn twice(x Int) Int {
    x * 2
}
fn chained(s String) Result<Int, String> {
    Result<Int, String>.ok(parsePositive(s) try twice)
}
report(chained("21"));
report(chained("no"));

-- try under a ternary (postfix binds tighter than ternary)
fn pickAdd(flag Bool, s String) Result<Int, String> {
    let n = flag ? parsePositive(s) try : 0;
    Result<Int, String>.ok(n + 1)
}
report(pickAdd(true, "7"));
report(pickAdd(false, "ignored"));
report(pickAdd(true, "bad"));

-- Same E, different ok type: err rebuilt against the return enum
fn describe(s String) Result<String, String> {
    let n = parsePositive(s) try;
    n > 100 ? Result<String, String>.ok("big") : Result<String, String>.ok("small")
}
fn reportS(r Result<String, String>) Void {
    match (r) {
        ok(v): println(v);
        err(m): println(m);
    }
}
reportS(describe("500"));
reportS(describe("3"));
reportS(describe("oops"));

-- Both-pointer-payload instantiation (heap repr coverage)
fn nonEmpty(s String) Result<String, String> {
    (s length) > 0 ? Result<String, String>.ok(s) : Result<String, String>.err("empty")
}
fn shout(s String) Result<String, String> {
    let v = nonEmpty(s) try;
    Result<String, String>.ok(v $ "!")
}
reportS(shout("hey"));
reportS(shout(""));

-- try as the value of an explicit return
fn viaReturn(s String) Result<Int, String> {
    return Result<Int, String>.ok(parsePositive(s) try);
}
report(viaReturn("11"));
report(viaReturn("x"));
