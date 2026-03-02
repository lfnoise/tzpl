-- Test: Any type basics

let a = any(5);
println(a);

let b = any("hello");
println(b);

let c = any(3.14);
println(c);

let d = any('foo);
println(d);

let e = any(true);
println(e);

-- as(Type) returns Option
let r1 = a as(Int);
println(r1);

let r2 = a as(String);
println(r2);

let r3 = b as(String);
println(r3);

-- toAnyArray converts a tuple to Array of Any
let arr = (1, "two", 'three, 4.0) toAnyArray;
println(arr);

-- match on Any values
fn describeAny(x Any) String {
    match(x) {
        i Int: "int: " $ toString(i);
        s String: "string: " $ s;
        f Float: "float: " $ toString(f);
        _: "other";
    }
}

println(describeAny(a));
println(describeAny(b));
println(describeAny(c));
println(describeAny(d));

-- nested any calls
println(any(1, any(2, 3)));
println(any(any("hello"), any(42)));
println(any(1, 2, any(3, 4), 5));
println(any(any(any(true))));
