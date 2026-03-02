-- Variadic function arguments

-- Untyped variadic: returns a Tuple of whatever types are passed
fn wrap(...xs) = xs;
wrap(1, "hello", true) println;
wrap(42) println;
wrap() println;

-- Typed variadic: returns an Array of the specified type
fn sumInts(...xs Int) Int {
    var total = 0;
    for (x : xs) {
        total = total + x;
    }
    total
}
sumInts(1, 2, 3) println;
sumInts(10, 20) println;
sumInts() println;

-- Mixed fixed and variadic params
fn greet(greeting String, ...names String) String {
    var result = greeting;
    for (name : names) {
        result = result $ " " $ name;
    }
    result
}
greet("Hello", "Alice", "Bob", "Charlie") println;
greet("Hi") println;

-- Variadic fmt (builtin): new natural syntax
"%^ + %^ = %^" fmt(1, 2, 3) println;

-- Pipeline with variadic fmt
"hello %^!" fmt("world") println;

-- Zero variadic args for fmt
"no placeholders" fmt() println;

-- Single variadic arg for fmt
"value: %^" fmt(42) println;

-- Untyped variadic with mixed types
fn first(...args) = args.0;
first(1, 2.5, "three") println;
first("hello", 42) println;
