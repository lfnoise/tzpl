-- Lists and the Cons Operator from Tzopilotl by Example

-- Create a list with the List constructor
let xs = List(1, 2, 3);
xs println;

-- Empty list with type annotation
let empty List<Int> = nil;
empty println;

-- Cons operator (::) prepend
let ys = 0 :: xs;
ys println;

-- Build from scratch with :: and nil
let zs = 1 :: 2 :: 3 :: nil;
zs println;

-- Pattern matching with cons
match (xs) {
    h :: t: {
        h println;
        t println;
    }
    nil: "empty" println;
}

-- Nested cons pattern
match (xs) {
    a :: b :: rest: {
        a println;
        b println;
        rest println;
    }
    _: "short" println;
}

-- For loop over a list
let lst = List(1, 2, 3);
for (x : lst) {
    print(x);
    print(" ");
}
println();

-- Infinite lists with iter
let powers = iter(1, fn(x Int) Int { x * 2 });
powers take(10) println;

let nats = iter(0, fn(x Int) Int { x + 1 });
nats take(5) println;

-- Works with any type
iter("a", fn(s String) String { s $ s }) take(4) println;
