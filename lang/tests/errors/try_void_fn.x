-- `try` in a Void fn: the enclosing fn must return Result/Option.
fn f(s String) Void {
    let n = parseInt(s) try;
    println(n);
}

f("1");
