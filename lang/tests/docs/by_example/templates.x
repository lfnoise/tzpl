-- Templates from Language X by Example

-- Template function with type parameter
fn identity<T>(x T) T = x;

-- Multiple type parameters
fn pick_first<A, B>(a A, b B) A = a;
fn pick_second<A, B>(a A, b B) B = b;

-- Type inferred from arguments
42 identity println;
"hello" identity println;

-- NOTE: Explicit type parameters on function calls (e.g. identity<Int>(42))
-- are not yet supported by the parser.

-- Templates work with arrays and tuples
fn wrap<T>(x T) [T] = [x];
fn pair<A, B>(a A, b B) (A, B) = (a, b);

5 wrap println;
println(pair(1, "hi"));

-- Template structs
struct Pair<T, U> {
    first T,
    second U
}

let p = Pair { first: 42, second: "hello" };
p.first println;
p.second println;

-- Explicit type arguments
let q = Pair<Int, Float> { first: 10, second: 2.5 };
q println;

-- Nested template types
struct Box<T> { value T }
let nested = Box { value: Pair { first: 1, second: 2 } };
nested.value.first println;

-- Template tuple structs
struct Wrapper<T>(T);
let w = Wrapper(42);
w println;
w.0 println;

let ws = Wrapper("hello");
ws println;

-- Template enums
enum Option<T> {
    some T,
    none
}

enum Either<L, R> {
    left L,
    right R
}

let x = Option.some(42);
let y = Option<Int>.none;
x println;
y println;

-- Pattern matching on template enums
fn unwrap<T>(opt Option<T>, default T) T {
    match (opt) {
        Option.some(v): return v;
        Option.none: return default;
    }
    default
}

println(unwrap(Option.some(10), 0));
println(unwrap(Option<Int>.none, 0));
