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
