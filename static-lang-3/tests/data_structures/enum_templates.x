-- Template enums

-- Option<T> is now built-in, no declaration needed

-- Inferred type from data case
let o1 = Option.some(42);
o1 println;

let o2 = Option.some("hello");
o2 println;

-- Explicit type args for no-data case
let o3 = Option<Int>.none;
o3 println;

let o4 = Option<String>.none;
o4 println;

-- Explicit type args for data case
let o5 = Option<Float>.some(3.14);
o5 println;

-- Pattern matching on template enums
fn describe_option(opt Option<Int>) Void {
    match (opt) {
        Option.some(x): x println;
        Option.none: "nothing" println;
    }
}
describe_option(Option.some(99));
describe_option(Option<Int>.none);

-- Two type param enum
enum Either<L, R> {
    Left L,
    Right R
}

let e1 = Either<Int, String>.Left(42);
e1 println;

let e2 = Either<Int, String>.Right("hello");
e2 println;

-- ordinal on template enums
ordinal(Option.some(42)) println;
ordinal(Option<Int>.none) println;
ordinal(Either<Int, String>.Left(42)) println;
ordinal(Either<Int, String>.Right("hello")) println;
