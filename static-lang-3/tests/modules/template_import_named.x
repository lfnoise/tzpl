-- Test: Named import of template functions, structs, enums
import template_lib.{identity, Box, Option, first};

-- Template function via named import
println(identity(99));
println(identity(2.71));

-- Template function with array
println(first(["a", "b", "c"]));

-- Template struct via named import
let b = Box { "wrapped" };
println(b.value);

-- Template enum via named import
let o = Option.some(3.14);
println(o);
let n = Option<String>.none;
println(n);
