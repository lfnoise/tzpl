-- print and println builtins

-- Basic println with different types
println(42);
println(3.14);
println(true);
println(false);
println("hello");
println('sym);

-- println with no args (empty line)
println();

-- Multi-argument println (space-separated)
println("hello", "world");
println(1, 2.5, true);
println("sum:", 1 + 2);

-- Pipeline syntax
42 println;
"pipeline" println;

-- print (no trailing newline, no flush)
print("no");
print(" new");
println("line");

-- println with complex types
println([1, 2, 3]);
println((1, "two", 3));
println((42,));

-- Auto-mapping with println
[1, 2, 3] @ println;

-- Auto-mapping with a function that calls println
fn show(x Int) = x println;
[10, 20, 30] show;

-- Multi-arg println with mixed types
println("The answer is", 42, "and pi is", 3.14);

-- Fraction
println(1/2);

-- println with string expressions
println("a" $ "b" $ "c");

-- print used for inline output
print("x=");
println(42);
