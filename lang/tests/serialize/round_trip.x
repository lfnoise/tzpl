-- TZV1 serialization: deserialize<T>(serialize(x)) round trips across the
-- value zoo, preserves DAG sharing and cycles, and produces deterministic
-- bytes regardless of construction order.

-- scalars and strings
(deserialize<Int>(serialize(42)) == 42) println;
(deserialize<Float>(serialize(3.25)) == 3.25) println;
(deserialize<Bool>(serialize(true)) == true) println;
(deserialize<String>(serialize("héllo wörld")) == "héllo wörld") println;
(deserialize<Symbol>(serialize('freq)) == 'freq) println;
(deserialize<Fraction>(serialize(1/3)) == 1/3) println;
(deserialize<Complex>(serialize(1.0 + 2.0i)) == 1.0 + 2.0i) println;

-- containers
(deserialize<[Int]>(serialize([1, 2, 3])) == [1, 2, 3]) println;
(deserialize<[Float]>(serialize([1.5, 2.5])) == [1.5, 2.5]) println;
(deserialize<[String]>(serialize(["a", "b"])) == ["a", "b"]) println;
(deserialize<[[Int]]>(serialize([[1], [2, 3]])) == [[1], [2, 3]]) println;
(deserialize<[Int]>(serialize([Int]())) == [Int]()) println;
(deserialize<[String: Int]>(serialize(["x": 1, "y": 2])) == ["x": 1, "y": 2]) println;
(deserialize<Set<Int>>(serialize(Set(1, 2, 3))) == Set(1, 2, 3)) println;
(deserialize<List<Int>>(serialize(List(1, 2, 3))) == List(1, 2, 3)) println;
(deserialize<#[Int]>(serialize(#[1, 2, 3])) == #[1, 2, 3]) println;
(deserialize<Ref<Int>>(serialize(&42)) == &42) println;
(deserialize<(Int, String, Float)>(serialize((1, "two", 3.5))) == (1, "two", 3.5)) println;

-- structs and enums (all payload shapes)
struct Point { x Float, y Float }
let p = Point { x: 1.5, y: 2.5 };
(deserialize<Point>(serialize(p)) == p) println;

struct Wrapper(Int);
(deserialize<Wrapper>(serialize(Wrapper(7))) == Wrapper(7)) println;

enum Shape {
    circle Float,
    rect (Float, Float),
    dot,
}
(deserialize<Shape>(serialize(Shape.rect((3.0, 4.0)))) == Shape.rect((3.0, 4.0))) println;
(deserialize<Shape>(serialize(Shape.dot)) == Shape.dot) println;

enum Color { red, green, blue }
(deserialize<Color>(serialize(Color.green)) == Color.green) println;

-- recursive enum type
enum Json {
    jnum Float,
    jstr String,
    jarr [Json],
    jnull,
}
let doc = Json.jarr([Json.jnum(1.0), Json.jarr([Json.jstr("deep")]), Json.jnull]);
(deserialize<Json>(serialize(doc)) == doc) println;

-- DAG sharing is preserved: two aliases of one Ref stay one object.
let shared = &10;
let pair = [shared, shared];
let out = deserialize<[Ref<Int>]>(serialize(pair));
out[0] <- 99;
*(out[1]) println;

-- cycles round trip and stay cyclic
enum Tree {
    node [Tree],
    leaf Int,
}
var ka = [Tree.leaf(1)];
ka push!(Tree.node(ka));
let kb = deserialize<[Tree]>(serialize(ka));
(kb == ka) println;
match (kb[1]) {
    Tree.node(inner): (inner == kb) println;
    _: println("not a node");
}

-- determinism: different construction orders give identical bytes
var m1 = ["a": 1];
m1["b"] = 2;
m1["c"] = 3;
var m2 = ["c": 3];
m2["b"] = 2;
m2["a"] = 1;
(serialize(m1) == serialize(m2)) println;
var s1 = Set(1, 2, 3);
var s2 = Set(3, 1, 2);
(serialize(s1) == serialize(s2)) println;
(serialize(doc) == serialize(doc)) println;
