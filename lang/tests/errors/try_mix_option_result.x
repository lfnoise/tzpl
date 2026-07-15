-- Option-try in a Result-returning fn (and the converse) is a type error.
import std.result.*;

fn optInResult(s String) Result<Int, String> {
    let n = parseInt(s) try;
    Result<Int, String>.ok(n)
}

fn resInOption(s String) Option<Int> {
    let r = Result<Int, String>.ok(1);
    let n = r try;
    Option.some(n)
}

println(optInResult("1"));
println(resInOption("1"));
