-- Logical operators

-- AND truth table
println(true && true);
println(true && false);
println(false && true);
println(false && false);

-- OR truth table
println(true || true);
println(true || false);
println(false || true);
println(false || false);

-- NOT
println(!true);
println(!false);
println(!!true);
println(!!false);

-- Short-circuit via an observable side effect. A mutable array is a
-- reference type, so a function appending to it mutates the shared object
-- (unlike a captured top-level scalar `var`, which is snapshotted by value
-- and would mask whether the RHS actually ran).
let log = [Int]();
fn mark(n Int) Bool {
    log push!(n);
    true
}

-- false && _ : RHS must NOT run (log stays empty)
false && mark(1);
log length println;        -- 0

-- true || _ : RHS must NOT run (log stays empty)
true || mark(2);
log length println;        -- 0

-- true && _ : RHS MUST run
true && mark(3);
log length println;        -- 1

-- false || _ : RHS MUST run
false || mark(4);
log length println;        -- 2

-- only the marks past the short-circuit point were logged, in order
log[0] println;            -- 3
log[1] println;            -- 4

-- Nested logical
println(!(true && false));
println(!(false || false));
println((true || false) && (true || false));
println((true && false) || (false && true));

-- Comparison with logical
println(1 < 2 && 3 > 0);
println(1 > 2 || 3 < 0);
println(1 == 1 && 2 == 2);
println(1 != 1 || 2 != 2);
