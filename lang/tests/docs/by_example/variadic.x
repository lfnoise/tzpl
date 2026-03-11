-- Variadic Arguments from Tzopilotl by Example

-- Untyped variadic: packs args into a Tuple
fn wrap(...xs) = xs;
wrap(1, "hello", true) println;
wrap(42) println;
wrap() println;

-- Typed variadic: packs args into an Array
fn sumInts(...xs Int) Int {
    var total = 0;
    for (x : xs) {
        total = total + x;
    }
    total
}
sumInts(1, 2, 3) println;
sumInts() println;

-- Fixed and variadic parameters can be mixed
fn greet(greeting String, ...names String) String {
    var result = greeting;
    for (name : names) {
        result = result $ " " $ name;
    }
    result
}
greet("Hello", "Alice", "Bob") println;
greet("Hi") println;
