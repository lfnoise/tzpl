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

-- Short-circuit: false && should not evaluate RHS
-- We verify this by using a function that would crash or produce output
var side_effect_count = 0;
fn side_effect() Bool {
    side_effect_count = side_effect_count + 1;
    true
}

false && side_effect();
side_effect_count println;

-- Short-circuit: true || should not evaluate RHS
side_effect_count = 0;
true || side_effect();
side_effect_count println;

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
