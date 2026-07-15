-- The operand's error type must match the enclosing fn's error type exactly.
import std.result.*;

fn inner() Result<Int, String> {
    Result<Int, String>.err("nope")
}

fn outer() Result<Int, Int> {
    let n = inner() try;
    Result<Int, Int>.ok(n)
}

println(outer());
