-- I/O Functions from Builtin_Functions.html

-- println prints values followed by a newline
println(42);
println("hello", "world");
println(1, 2.5, true);
println();

-- print does not add a newline
print("x=");
println(42);

-- Pipeline syntax
42 println;
"hello" println;

-- Auto-mapping: print each element on its own line
[1, 2, 3] @ println;

-- getListPrintLimit / setListPrintLimit
getListPrintLimit() println;
let old = setListPrintLimit(3);
(1..10) toList println;
setListPrintLimit(old);
