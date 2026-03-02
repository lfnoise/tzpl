-- Test 1: Wildcard import of template functions, structs, enums
import template_lib.*;

-- Template function via wildcard
println(identity(42));
println(identity("hello"));

-- Template function with array param via wildcard
println(first([10, 20, 30]));

-- make_pair template function
println(make_pair(1, "two"));

-- Non-template overload preferred for Int+Int
println(add(3, 4));
-- Template used for Float+Float
println(add(1.5, 2.5));

-- Template struct via wildcard
let b = Box { 99 };
println(b.value);

let p = Pair { "hello", 3.14 };
println(p.first);
println(p.second);

-- Template enum via wildcard
let o1 = Option.some(42);
println(o1);
let o2 = Option<Int>.none;
println(o2);
