-- `try` requires a Result or Option operand.
import std.result.*;

fn f(x Int) Result<Int, String> {
    let y = x try;
    Result<Int, String>.ok(y)
}

println(f(1));
