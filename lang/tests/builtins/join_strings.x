-- join([String]) and join([String], sep) concatenate string arrays.

let parts = ["alpha", "beta", "gamma"];
parts join println;
parts join(", ") println;
parts join("") println;

-- single element and empty arrays
["solo"] join("-") println;
let empty [String] = [];
empty join println;
empty join("-") println;
"<" $ (empty join) $ ">" |> println;

-- separators longer than elements
["a", "b"] join(" -- ") println;

-- nested-array join (flatten one level) still works
[[1, 2], [3, 4]] join println;

-- join(List<String>) -> String, the List analogue (incl. lazy sources)
List("ab", "cd") join println;
(1..3) toList map(fn(x Int) String { x toString }) join println;

-- the String forms never hide the one-level flatten: an element type of
-- [String] takes the [[T]] path
[["a", "b"], ["c"]] join println;
[["a", "b"], ["c"]] join join println;
List(List("a"), List("b", "c")) join println;
