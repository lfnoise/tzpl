-- Phase 3: NullablePtrEnum -- 2-case enum with one Void + one Pointer payload
-- represented as a nullable Obj* at runtime.

-- Built-in Option<String> is the canonical case.
let s = Option<String>.some("hello");
let n = Option<String>.none;

println(s);                 -- Option<String>.some(hello)
println(n);                 -- Option<String>.none
println(s isSome);          -- true
println(s isNone);          -- false
println(n isSome);          -- false
println(n isNone);          -- true
println(s unwrap);          -- hello
println(n unwrapOr("dflt"));-- dflt
println(s unwrapOr("dflt"));-- hello

-- ordinal / tag (Option cases are some=0, none=1)
println(s ordinal);         -- 0
println(n ordinal);         -- 1
println(s tag);             -- 'some
println(n tag);             -- 'none

-- match arms
fn describe(o Option<String>) String {
    match (o) {
        Option.some(x): x;
        Option.none: "missing";
    }
}
println(describe(s));       -- hello
println(describe(n));       -- missing

-- Equality (content-based via inner type)
let a = Option<String>.some("x");
let b = Option<String>.some("x");
println(a == b);            -- true (string content equality)
println(a == n);            -- false

-- Storage in a [Option<String>] array (ObjArray since Option<String> is a pointer-storing type)
let arr = [s, n, Option<String>.some("two"), Option<String>.none];
println(arr);               -- [Option<String>.some(hello), Option<String>.none, ..., ...]

-- Pass through a generic identity
fn id<T>(x T) T = x;
println(id(s));             -- Option<String>.some(hello)
println(id(n));             -- Option<String>.none

-- Storage in a struct field
struct Box { name String, opt Option<String> }
let b1 = Box { name: "first", opt: s };
let b2 = Box { name: "second", opt: n };
println(b1);                -- Box { name: first, opt: Option<String>.some(hello) }
println(b2);                -- Box { name: second, opt: Option<String>.none }

-- Map subscript returns Option<V>
let m = ["k": "v1", "j": "v2"];
println(m["k"]);            -- Option<String>.some(v1)
println(m["nope"]);         -- Option<String>.none
println(m["k"] unwrap);     -- v1
println(m["nope"] isNone);  -- true

-- Any boxing
println(any(s));            -- Any(Option<String>.some(hello), Option<String>)
