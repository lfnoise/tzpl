-- Template structs

-- Single type param
struct Box<T> { value T }
let b1 = Box { 42 };
b1 println;
b1.value println;

let b2 = Box { "hello" };
b2 println;
b2.value println;

-- Two type params
struct Pair<T, U> { first T, second U }
let p = Pair { 1, 3.14 };
p println;
p.first println;
p.second println;

-- Named field construction
let p2 = Pair { first: "hello", second: 42 };
p2 println;

-- Explicit type args
let p3 = Pair<Int, Float> { 10, 2.5 };
p3 println;

-- Template function using template struct
fn get_first<T, U>(p Pair<T, U>) T = p.first;
fn get_second<T, U>(p Pair<T, U>) U = p.second;
let p4 = Pair { 100, "abc" };
p4 get_first println;
p4 get_second println;

-- Nested template types
let nested = Box { Pair { 1, 2 } };
nested println;
nested.value println;
nested.value.first println;
nested.value.second println;

-- Three type params
struct Triple<A, B, C> { a A, b B, c C }
let t = Triple { 1, "two", 3.14 };
t.a println;
t.b println;
t.c println;

-- Pattern matching on template structs
fn describe_pair(p Pair<Int, String>) {
    match (p) {
        Pair { first: n, second: s }: {
            n println;
            s println;
        }
    }
}
describe_pair(Pair { 42, "world" });
