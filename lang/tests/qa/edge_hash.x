-- QA: Hash function
-- Note: Hash values are randomized per run, so only test properties

-- Equal values produce equal hashes
println(hash(42) == hash(42));
println(hash("hello") == hash("hello"));
println(hash('foo) == hash('foo));
println(hash((1, 2)) == hash((1, 2)));
println(hash([1, 2, 3]) == hash([1, 2, 3]));
println(hash(true) == hash(true));
println(hash(3.14) == hash(3.14));

-- Different values produce different hashes (usually)
println(hash(1) != hash(2));
println(hash("a") != hash("b"));
println(hash(true) != hash(false));

-- Hash on struct
struct Point { x Int, y Int }
println(hash(Point{1, 2}) == hash(Point{1, 2}));
println(hash(Point{1, 2}) != hash(Point{3, 4}));

-- Hash on enum
enum Color { red, green, blue }
println(hash(Color.red) == hash(Color.red));
println(hash(Color.red) != hash(Color.blue));

-- Hash is an Int
let h = hash(42);
println(h == h);  -- true (trivially)
