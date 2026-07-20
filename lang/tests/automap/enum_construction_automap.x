-- Auto-mapping over enum-case construction: Enum.case([...]) builds one
-- enum value per array element, mirroring struct / tuple-struct construction.

enum Enum { a Int, b Int, }

-- Array payload auto-maps into an array of enums
println(Enum.a([1, 2, 3, 4]));
-- Expected: [Enum.a(1), Enum.a(2), Enum.a(3), Enum.a(4)]

-- Scalar payload still constructs a single value (no auto-map)
println(Enum.a(99));
-- Expected: Enum.a(99)

-- Int-to-Float element promotion in the payload
enum FloatCase { f Float, }
println(FloatCase.f([1, 2, 3]));
-- Expected: [FloatCase.f(1.0), FloatCase.f(2.0), FloatCase.f(3.0)]

-- Option-like enum (NullablePtrEnum repr)
enum Opt { some Int, none Void, }
println(Opt.some([10, 20, 30]));
-- Expected: [Opt.some(10), Opt.some(20), Opt.some(30)]

-- Struct payload (multi-word / heap repr)
struct P { x Int, y Int, }
enum Shape { at P, nowhere Void, }
println(Shape.at([P { x: 1, y: 2 }, P { x: 3, y: 4 }]));
-- Expected: [Shape.at(P { x: 1, y: 2 }), Shape.at(P { x: 3, y: 4 })]

-- The produced array is fully usable downstream (auto-mapped match)
fn unwrap(e Enum) Int {
    match (e) {
        Enum.a(n): n * 100;
        Enum.b(n): n;
    }
}
println(unwrap(Enum.a([5, 6, 7])));
-- Expected: [500, 600, 700]
