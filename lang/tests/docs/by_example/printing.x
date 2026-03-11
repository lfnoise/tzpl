-- Printing examples from Language X by Example

-- println prints its arguments separated by spaces, followed by a newline
println(42);
println("hello", "world");
println(1, 2.5, true);
println();

-- print is the same but without a trailing newline
print("x=");
println(42);

-- Pipeline syntax works too
42 println;
"hello" println;

-- Auto-map with @ to print each element on its own line
[1, 2, 3] @ println;
