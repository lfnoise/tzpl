-- QA: Edge cases in the Any type

-- Wrapping values
let a = any(42);
a println;

let b = any("hello");
b println;

let c = any(true);
c println;

let d = any(3.14);
d println;

-- as(Type) casting
let x = any(42);
let y = x as(Int);
y println;

let s = any("hello");
let s2 = s as(String);
s2 println;

-- Match on Any
fn describe_any(val Any) String {
    match (val) {
        x Int: return fmt("int: %^", x);
        x String: return fmt("str: %^", x);
        x Bool: return fmt("bool: %^", x);
        x Float: return fmt("float: %^", x);
        _: return "unknown";
    }
    "unreachable"
}

describe_any(any(42)) println;
describe_any(any("hello")) println;
describe_any(any(true)) println;
describe_any(any(3.14)) println;

-- Any in collections
let arr = [any(1), any(2), any(3)];
arr length println;
arr[0] println;

-- Unwrap any
let wrapped = any(42);
let unwrapped = wrapped as(Int);
unwrapped println;
