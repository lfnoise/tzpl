import template_lib.*;

-- Template function via wildcard
42 identity println;
"hello" identity println;

-- Template function with array param
[10, 20, 30] first println;

-- make_pair
make_pair(1, "two") println;

-- Non-template overload preferred for Int+Int
add(3, 4) println;
-- Template used for Float+Float
add(1.5, 2.5) println;

-- Template struct
let b = Box { 99 };
b.value println;

let p = Pair { "hello", 3.14 };
p.first println;
p.second println;

-- Template enum
let o1 = Option.some(42);
o1 println;
let o2 = Option<Int>.none;
o2 println;
