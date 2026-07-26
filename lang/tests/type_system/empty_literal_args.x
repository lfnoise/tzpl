-- An empty collection literal has no element type of its own, so in argument
-- position it takes the type of the parameter it is passed to.

fn takesMap(m [Int:Int]) Int { m length }
fn takesArray(a [Int]) Int { a length }
fn takesList(l List<Int>) Int { l length }
fn takesSet(s Set<Int>) Int { s length }
fn takesPVec(v #[Int]) Int { v length }
fn takesPMap(m #[Int:Int]) Int { m length }

println([:] takesMap);
println(takesMap([:]));
println([] takesArray);
println(List() takesList);
println(Set() takesSet);
println(#[] takesPVec);
println(#[:] takesPMap);

-- The deduced value is a real, mutable collection of the parameter's type
fn fill(m [Int:Int]) [Int:Int] { m[1] = 42; m }
println(fill([:]));

-- Deduction alongside arguments whose types are known
fn withSibling(m [Int:Int], n Int) Int { n }
println(withSibling([:], 7));

-- Overloads are told apart by the other arguments
fn pick(m [Int:Int], x Int) String { "int map" }
fn pick(m [String:String], x String) String { "string map" }
println(pick([:], 1));
println(pick([:], "s"));

-- A lambda states its parameter types outright
let f = fn(m [Int:Int]) Int { m length };
println(f([:]));

-- Positions that already worked keep working
let annotated [Int:Int] = [:];
println(annotated takesMap);
fn returnsEmpty() [Int:Int] { [:] }
println(returnsEmpty());

-- Struct fields take their declared type, named or positional
struct Holder { m [Int:Int], n Int }
println(Holder { m: [:], n: 1 });
println(Holder { [:], 2 });
struct Box<T> { v T }
println(Box<[Int:Int]> { v: [:] });

-- Nested inside another literal, from the annotation
let nestedArr [[Int:Int]] = [[:]];
println(nestedArr);
let nestedMap [String:[Int:Int]] = ["a": [:]];
println(nestedMap);
let nestedList List<[Int:Int]> = List([:]);
println(nestedList);
let nestedSet Set<[Int]> = Set([]);
println(nestedSet);
let nestedTuple (Int, [Int:Int]) = (1, [:]);
println(nestedTuple);
let nestedPVec #[[Int:Int]] = #[[:]];
println(nestedPVec);
let mapKeys [[Int]:[Int]] = [[]: []];
println(mapKeys);

-- Or from a sibling element, whichever side it is on
println([[1, 2], []]);
println([[], [1, 2]]);
println(["a": [1:2], "b": [:]]);
println(["a": [:], "b": [1:2]]);
