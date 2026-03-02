-- Hashing and Equality from Builtin_Functions.html

-- Hash values (deterministic for value types)
hash(42) println;
hash(3.14) println;

-- Equal values produce equal hashes
println(hash((1, 2)) == hash((1, 2)));
println(hash([1, 2, 3]) == hash([1, 2, 3]));

-- Struct equality
struct Point { x Int, y Int }
let p1 = Point{1, 2};
println(p1 == Point{1, 2});
println(p1 == Point{3, 4});

-- Tuple equality
println((1, 2) == (1, 2));

-- Array equality
println([1, 2, 3] == [1, 2, 3]);

-- Symbol equality
println('foo == 'foo);

-- Enum equality
enum Color { red, green, blue }
println(Color.red == Color.red);
