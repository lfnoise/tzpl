-- Test: Module-qualified template function calls
import template_lib;

-- Template function via module-qualified call
println(template_lib.identity(42));
println(template_lib.identity("world"));

-- Template function with array param
println(template_lib.first([100, 200]));

-- make_pair
println(template_lib.make_pair(3.14, "pi"));

-- Non-template overload preferred for Int+Int
println(template_lib.add(10, 20));
-- Template used for Float+Float
println(template_lib.add(1.0, 2.0));
