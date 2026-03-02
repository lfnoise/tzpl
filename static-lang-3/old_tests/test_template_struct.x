-- Template struct: basic declaration and inferred construction
struct Pair<T, U> {
    first T,
    second U
}

let p = Pair { 1, 3.14 };
println(p);
println(p.first);
println(p.second);

-- Template struct: named field construction
let p2 = Pair { first: "hello", second: 42 };
println(p2);
println(p2.first);
println(p2.second);

-- Template struct: explicit type args
let p3 = Pair<Int, Float> { 10, 2.5 };
println(p3);
println(p3.first);
println(p3.second);

-- Template struct with single type param
struct Box<T> {
    value T
}

let b1 = Box { 42 };
println(b1);
println(b1.value);

let b2 = Box { "hello" };
println(b2);
println(b2.value);

-- Template enum: basic declaration
enum Option<T> {
    some T,
    none
}

-- Template enum construction with data case (inferred)
let o1 = Option.some(42);
println(o1);

let o2 = Option.some("hello");
println(o2);

-- Template enum construction with explicit type args (no-data case)
let o3 = Option<Int>.none;
println(o3);

let o4 = Option<String>.none;
println(o4);

-- Template enum with explicit type args (data case)
let o5 = Option<Float>.some(3.14);
println(o5);

-- Pattern matching on template enums
fn describe_option(opt Option<Int>) Void {
    match (opt) {
        Option.some(x): println(x);
        Option.none: println("nothing");
    }
}

describe_option(Option.some(99));
describe_option(Option<Int>.none);

-- Pattern matching on template structs
fn describe_pair(p Pair<Int, String>) {
    match (p) {
        Pair { first: n, second: s }: {
            println(n);
            println(s);
        }
    }
}

describe_pair(Pair { 42, "world" });

-- Template function using template struct
fn get_first<T, U>(p Pair<T, U>) T = p.first;
fn get_second<T, U>(p Pair<T, U>) U = p.second;

let p4 = Pair { 100, "abc" };
println(get_first(p4));
println(get_second(p4));

-- Nested template types
let nested = Box { Pair { 1, 2 } };
println(nested);
println(nested.value);
println(nested.value.first);
println(nested.value.second);

-- Cache hit: reuse same monomorphization
let p5 = Pair { 3, 1.0 };
let p6 = Pair { 7, 2.0 };
println(p5.first);
println(p6.first);

-- Multiple type params with enum
enum Either<L, R> {
    Left L,
    Right R
}

let e1 = Either<Int, String>.Left(42);
println(e1);

let e2 = Either<Int, String>.Right("hello");
println(e2);

-- Field access on template struct instance
struct Triple<A, B, C> {
    a A,
    b B,
    c C
}

let t = Triple { 1, "two", 3.14 };
println(t.a);
println(t.b);
println(t.c);
