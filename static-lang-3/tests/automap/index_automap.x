-- Auto-mapped index access

-- Array indexed by array of indices
[1, 2, 3, 4][[3, 1, 2, 0]] println;

-- Map indexed by array of keys (returns [Option<Int>])
['a: 1, 'b: 2][['b, 'a, 'a, 'b]] @ unwrap println;

-- @ on object: array of arrays indexed by scalar
[[1, 2, 3], [4, 5, 6], [7, 8, 9]] @ [1] println;

-- @ on object: array of maps indexed by scalar key (returns [Option<Int>])
[['a: 1, 'b: 2], ['a: 3, 'b: 4], ['a: 5, 'b: 6]] @ ['b] @ unwrap println;

-- Combined: @ on object + array of indices (recursive auto-mapping)
[[1, 2, 3], [4, 5, 6], [7, 8, 9]] @ [[2, 0, 1]] println;

-- Recursive auto-mapping in function calls
fn pair(a Int, b Int) = (a, b);
pair([1, 2, 3] @, [4, 5, 6]) println;

-- Deep explicit @@ + implicit auto-mapping
pair([[1, 2], [3, 4]] @@, [5, 6]) println;
