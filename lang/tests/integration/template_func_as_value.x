-- Template function used as a value argument.
-- The type checker should infer template type params from context.

fn twice(x) = 2 * x;
iter(1, twice) take(10) println;

fn add3(x) = x + 3;
iter(0, add3) take(5) println;
