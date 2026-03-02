-- Test float capture
let pi = 3.14159;
let double_pi = fn() Float { pi * 2.0 };
println(double_pi());

-- Test string capture
let greeting = "hello";
let greet = fn(name String) String { greeting $ " " $ name };
println(greet("world"));

-- Test bool return with int capture
let threshold = 10;
let isAbove = fn(n Int) Bool { n > threshold };
println(isAbove(5));
println(isAbove(15));
