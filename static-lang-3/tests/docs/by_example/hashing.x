-- Hashing from Language X by Example

-- Hash works on all types (values are deterministic for value types)
hash(42) println;
hash(3.14) println;

-- Equal values produce equal hashes
println(hash((1, 2)) == hash((1, 2)));
println(hash([1, 2, 3]) == hash([1, 2, 3]));

-- Structs are hashable
struct Key { a Int, b Int }
println(hash(Key{1, 2}) == hash(Key{1, 2}));
