-- QA: Edge cases in Map operations

-- Single entry map
let m1 = ["a": 1];
m1 length println;
m1 println;

-- Map with same key multiple times in literal
let m2 = ["a": 1, "a": 2];
m2["a"] unwrap println;  -- last one wins? Or first?
m2 length println;

-- Map with integer keys
let m3 = [0: "zero", 1: "one", -1: "neg_one"];
m3[0] unwrap println;
m3[1] unwrap println;
m3[-1] unwrap println;
m3 length println;

-- Map iteration via keys/values
let m4 = ["x": 10, "y": 20, "z": 30];
m4 keys() length println;
m4 values() length println;

-- Map merge with empty
let empty [String: Int] = [:];
let m5 = merge(empty, ["a": 1]);
m5 length println;
m5["a"] unwrap println;

let m6 = merge(["a": 1], empty);
m6 length println;
m6["a"] unwrap println;

-- Map of maps (nested)
let nested = ["outer": ["inner": 42]];
nested["outer"] unwrap println;

-- Map put returns immutable copy
let m7 = ["a": 1];
let m8 = m7 put("b", 2);
m7 length println;  -- original unchanged
m8 length println;

-- Map remove key that doesn't exist
let m9 = ["a": 1, "b": 2];
let m10 = m9 remove("c");
m10 length println;  -- should be same as m9
m10 contains("a") println;
m10 contains("b") println;

-- Float keys
let m11 = [1.0: "one", 2.0: "two"];
m11[1.0] unwrap println;
m11[2.0] unwrap println;
m11 length println;

-- Bool keys
let m12 = [true: 1, false: 0];
m12[true] unwrap println;
m12[false] unwrap println;
m12 length println;
