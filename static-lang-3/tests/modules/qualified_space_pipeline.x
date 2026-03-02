-- Module-qualified functions in space pipeline syntax
import math_utils;

-- basic: x mod.func -> mod.func(x)
5 math_utils.square println;

-- chained: x mod.f mod.g -> mod.g(mod.f(x))
2 math_utils.square math_utils.cube println;

-- mixed with non-module pipeline
fn add_one(x Int) Int = x + 1;
5 math_utils.square add_one println;

-- nested syntax still works alongside pipeline
println(math_utils.square(6));
