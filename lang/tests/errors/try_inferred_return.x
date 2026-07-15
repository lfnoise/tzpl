-- `try` requires an explicitly declared Result/Option return type; it cannot
-- participate in return-type inference (it only knows the error side).
fn f(s String) {
    let n = parseInt(s) try;
    Option.some(n)
}

println(f("1"));
