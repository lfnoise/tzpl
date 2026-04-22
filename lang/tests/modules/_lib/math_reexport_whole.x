-- `export math_utils;` makes the alias `math_utils` both local and
-- public. Inside this module we can call `math_utils.square(...)`; any
-- module that wildcard- or named-imports this one will also receive the
-- alias so they can write `math_utils.square(...)` too.
export math_utils;

fn twice_squared(x Int) Int = math_utils.square(x) * math_utils.square(x);
