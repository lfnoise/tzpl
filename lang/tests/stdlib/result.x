-- std.result: Result<T, E> combinators.
import std.result.*;

let r1 = Result<Int, String>.ok(7);
let r2 = Result<Int, String>.err("boom");

r1 isOk println;
r2 isOk println;
r1 isErr println;
r2 isErr println;
r1 unwrapOr(0) println;
r2 unwrapOr(0) println;
r2 unwrapOrElse(fn(e String) Int { e length }) println;
r1 mapOk(fn(x Int) Int { x * 2 }) unwrapOr(0) println;
r2 mapErr(fn(e String) String { e toUpper }) errOption unwrap println;
r1 okOption unwrap println;
r1 unwrapResult println;
r2 okOption isNone println;
r1 errOption isNone println;
