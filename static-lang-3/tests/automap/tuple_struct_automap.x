-- Tuple struct auto-mapping

struct Point(Int, Int);

-- Implicit: single array arg + scalar
Point([10, 20, 30], 4) println;

-- Implicit: scalar + array
Point(5, [100, 200, 300]) println;

-- Implicit: both arrays (zipped)
Point([1, 2, 3], [10, 20, 30]) println;

-- Implicit: different lengths (min)
Point([1, 2, 3, 4, 5], [10, 20, 30]) println;

-- Implicit: single-element array
Point([42], 7) println;

-- Explicit @ on one arg
Point([1, 2, 3] @, 99) println;

-- Int-to-Float promotion in auto-mapped arg
struct FPair(Float, Float);
FPair([1, 2, 3], 0.0) println;

-- Three-field tuple struct
struct RGB(Int, Int, Int);
RGB([255, 128, 0], [10, 20, 30], 1) println;

-- Template tuple struct with explicit @ auto-mapping
struct Wrapper<T>(T);
Wrapper([10, 20, 30] @) println;

-- Template multi-field tuple struct with explicit @
struct Pair<A, B>(A, B);
Pair([1, 2, 3] @, "x") println;

-- Cartesian @1/@2 tuple struct construction
Point([1, 2, 3] @1, [10, 20] @2) println;

-- Cartesian @1/@2/@3 tuple struct construction (3-level)
struct Vec3(Int, Int, Int);
Vec3([1, 2] @1, [3, 4] @2, [5, 6] @3) println;
