-- Phase 2: empty enums become i64 case indices (DiscriminantEnum repr).

enum Color {
    red,
    green,
    blue
}

-- Construction prints as EnumName.caseName
println(Color.red);
println(Color.green);
println(Color.blue);

-- ordinal() returns the case index
println(ordinal(Color.red));
println(ordinal(Color.green));
println(ordinal(Color.blue));

-- tag() returns the case name symbol
println(tag(Color.red));
println(tag(Color.blue));

-- Pattern match
fn describe(c Color) String {
    match (c) {
        red:   "red";
        green: "green";
        blue:  "blue";
    }
}
println(describe(Color.red));
println(describe(Color.green));
println(describe(Color.blue));

-- Equality
println(Color.red == Color.red);
println(Color.red == Color.blue);

-- Stored in an array (PodArray<i64> since DiscriminantEnum is not an Obj*)
let arr = [Color.red, Color.blue, Color.green, Color.red];
println(arr);
println(arr[0]);
println(arr[2]);

-- Stored in a struct field
struct Pixel { tag Color, x Int, y Int }
let p = Pixel { tag: Color.green, x: 3, y: 4 };
println(p);

-- Pattern match through struct field
match (p) {
    Pixel { tag: green, x: xx, y: yy }: println("green pixel" fmt());
    _: println("other");
}

-- Generic identity function (T = Color)
fn id<T>(x T) T = x;
println(id(Color.blue));

-- Single-case empty enum collapses to constant 0
enum Only { sole }
println(Only.sole);
println(ordinal(Only.sole));

-- Use as map key (PodArray<i64> hash path)
let m = [Color.red: 100, Color.blue: 200];
println(m[Color.red]);
println(m[Color.blue]);

-- Any boxing preserves type for printing
println(any(Color.green));
